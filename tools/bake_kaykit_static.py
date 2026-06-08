#!/usr/bin/env python3
"""Bake a rigged KayKit character .glb into flat, static .glb frame(s).

The course framework's glTF loader renders meshes statically (concatenates every
primitive's raw vertices, applies one node transform, no skinning), so rigged
characters can't be drawn directly. This bakes the geometry offline:

  - no animation -> bind pose (skinned meshes are already in model space; the
    rigid hat gets its node's global transform);
  - with an animation clip -> linear-blend skinning is evaluated at sampled
    times, so each output frame is a static snapshot of the pose. Several frames
    form a flipbook the engine can swap between (reusing the lit/unlit mesh-swap
    path) for animation without any in-engine skinning.

The embedded base-colour texture is carried over so the per-object texture path
keeps working, and each output has a single root node + one merged mesh.

Usage:
  bind pose : bake_kaykit_static.py <mesh.glb> <out.glb>
  one pose  : bake_kaykit_static.py <mesh.glb> <out.glb> --anim <a.glb> --clip Idle_A
  flipbook  : bake_kaykit_static.py <mesh.glb> <out.glb> --anim <a.glb> --clip Idle_A --frames 24
"""
import json
import struct
import sys

import numpy as np

COMP = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2),
        5125: ("I", 4), 5126: ("f", 4)}
NCOMP = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def read_glb(path):
    d = open(path, "rb").read()
    assert struct.unpack_from("<I", d, 0)[0] == 0x46546C67, "not a glb"
    off, js, bin_ = 12, None, b""
    while off < len(d):
        clen, ctype = struct.unpack_from("<II", d, off); off += 8
        chunk = d[off:off + clen]; off += clen
        if ctype == 0x4E4F534A:
            js = json.loads(chunk)
        elif ctype == 0x004E4942:
            bin_ = chunk
    return js, bin_


def accessor(js, bin_, idx):
    a = js["accessors"][idx]; bv = js["bufferViews"][a["bufferView"]]
    fmt, size = COMP[a["componentType"]]; nc = NCOMP[a["type"]]
    base = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    stride = bv.get("byteStride", size * nc)
    out = np.empty((a["count"], nc), dtype=np.float64 if fmt == "f" else np.int64)
    for i in range(a["count"]):
        out[i] = struct.unpack_from("<" + fmt * nc, bin_, base + i * stride)
    return out


def quat_mat(q):
    x, y, z, w = q
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def trs_mat(T, R, S):
    M = np.eye(4); M[:3, :3] = quat_mat(R) * np.asarray(S); M[:3, 3] = T
    return M


def node_local(n):
    if "matrix" in n:
        return np.array(n["matrix"], float).reshape(4, 4).T
    return trs_mat(n.get("translation", [0, 0, 0]),
                   n.get("rotation", [0, 0, 0, 1]), n.get("scale", [1, 1, 1]))


def fk(js, locals_):
    parent = {}
    for i, n in enumerate(js["nodes"]):
        for c in n.get("children", []):
            parent[c] = i
    glob = [None] * len(js["nodes"])

    def g(i):
        if glob[i] is None:
            glob[i] = g(parent[i]) @ locals_[i] if i in parent else locals_[i]
        return glob[i]
    return [g(i) for i in range(len(js["nodes"]))]


def slerp(a, b, f):
    a = np.asarray(a, float); b = np.asarray(b, float)
    d = float(np.dot(a, b))
    if d < 0:
        b = -b; d = -d
    if d > 0.9995:
        r = a + f * (b - a)
    else:
        th = np.arccos(d); s = np.sin(th)
        r = (np.sin((1 - f) * th) * a + np.sin(f * th) * b) / s
    return r / np.linalg.norm(r)


def sample(anim, abin, clip, ch, node_idx, t):
    n = anim["nodes"][node_idx]
    T = np.array(n.get("translation", [0, 0, 0]), float)
    R = np.array(n.get("rotation", [0, 0, 0, 1]), float)
    S = np.array(n.get("scale", [1, 1, 1]), float)
    for path, sidx in ch.get(node_idx, {}).items():
        s = clip["samplers"][sidx]
        times = accessor(anim, abin, s["input"]).reshape(-1)
        vals = accessor(anim, abin, s["output"])
        if t <= times[0]:
            v = vals[0]
        elif t >= times[-1]:
            v = vals[-1]
        else:
            i = int(np.searchsorted(times, t)) - 1
            f = (t - times[i]) / (times[i + 1] - times[i])
            v = slerp(vals[i], vals[i + 1], f) if len(vals[i]) == 4 else vals[i] + (vals[i + 1] - vals[i]) * f
        if path == "translation":
            T = v
        elif path == "rotation":
            R = v
        elif path == "scale":
            S = v
    return trs_mat(T, R, S)


def write_glb(path, P, N, UV, IDX, png, mime):
    blob = bytearray(); views = []

    def add(data, target=None):
        while len(blob) % 4:
            blob.append(0)
        off = len(blob); blob.extend(data)
        bv = {"buffer": 0, "byteOffset": off, "byteLength": len(data)}
        if target:
            bv["target"] = target
        views.append(bv); return len(views) - 1

    vp = add(P.tobytes(), 34962); vn = add(N.tobytes(), 34962)
    vu = add(UV.tobytes(), 34962); vi = add(IDX.tobytes(), 34963); vimg = add(png)
    acc = [
        {"bufferView": vp, "componentType": 5126, "count": len(P), "type": "VEC3",
         "min": P.min(0).tolist(), "max": P.max(0).tolist()},
        {"bufferView": vn, "componentType": 5126, "count": len(N), "type": "VEC3"},
        {"bufferView": vu, "componentType": 5126, "count": len(UV), "type": "VEC2"},
        {"bufferView": vi, "componentType": 5125, "count": len(IDX), "type": "SCALAR"}]
    gltf = {"asset": {"version": "2.0", "generator": "bake_kaykit_static"},
            "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": "baked"}],
            "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                       "indices": 3, "material": 0}]}],
            "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0},
                          "metallicFactor": 0.0, "roughnessFactor": 1.0}}],
            "textures": [{"source": 0, "sampler": 0}],
            "images": [{"bufferView": vimg, "mimeType": mime}], "samplers": [{}],
            "accessors": acc, "bufferViews": views, "buffers": [{"byteLength": len(blob)}]}
    jb = json.dumps(gltf, separators=(",", ":")).encode()
    while len(jb) % 4:
        jb += b" "
    while len(blob) % 4:
        blob.append(0)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(jb) + 8 + len(blob)))
        f.write(struct.pack("<II", len(jb), 0x4E4F534A)); f.write(jb)
        f.write(struct.pack("<II", len(blob), 0x004E4942)); f.write(blob)


def bake_frame(mage, mbin, anim, abin, clip, ch, t):
    """Return (P, N, UV, IDX) for one posed frame (or bind pose if clip is None)."""
    skin = mage["skins"][0]
    ibm = accessor(mage, mbin, skin["inverseBindMatrices"]).reshape(-1, 4, 4).transpose(0, 2, 1)
    joint_name = [mage["nodes"][j].get("name") for j in skin["joints"]]

    if clip is not None:
        a_name2i = {n.get("name"): i for i, n in enumerate(anim["nodes"])}
        a_locals = [node_local(n) for n in anim["nodes"]]
        for ni in ch:
            a_locals[ni] = sample(anim, abin, clip, ch, ni, t)
        a_glob = fk(anim, a_locals)
        a_rest = fk(anim, [node_local(n) for n in anim["nodes"]])
        skin_mats = np.stack([a_glob[a_name2i[joint_name[k]]] @ ibm[k] for k in range(len(joint_name))])
        head = a_name2i["head"]
        hat_delta = a_glob[head] @ np.linalg.inv(a_rest[head])
    mage_glob = fk(mage, [node_local(n) for n in mage["nodes"]])

    mesh_node = {n["mesh"]: i for i, n in enumerate(mage["nodes"]) if "mesh" in n}
    P, NN, UV, IDX = [], [], [], []
    voff = 0
    for mi, mesh in enumerate(mage["meshes"]):
        ni = mesh_node[mi]; skinned = "skin" in mage["nodes"][ni]
        for prm in mesh["primitives"]:
            at = prm["attributes"]
            pos = accessor(mage, mbin, at["POSITION"])
            nrm = accessor(mage, mbin, at["NORMAL"]) if "NORMAL" in at else np.zeros_like(pos)
            uv = accessor(mage, mbin, at["TEXCOORD_0"]) if "TEXCOORD_0" in at else np.zeros((len(pos), 2))
            if clip is None:
                M = np.eye(4) if skinned else mage_glob[ni]
                pos = (M @ np.c_[pos, np.ones(len(pos))].T).T[:, :3]
                nrm = (np.linalg.inv(M[:3, :3]).T @ nrm.T).T
            elif skinned:
                J = accessor(mage, mbin, at["JOINTS_0"]).astype(int)
                W = accessor(mage, mbin, at["WEIGHTS_0"]); W = W / W.sum(1, keepdims=True)
                M = np.einsum("vj,vjab->vab", W, skin_mats[J])  # per-vertex blended 4x4
                pos = np.einsum("vab,vb->va", M[:, :3, :3], pos) + M[:, :3, 3]
                nrm = np.einsum("vab,vb->va", M[:, :3, :3], nrm)
            else:  # rigid hat: follow the head bone's animated delta
                MB = hat_delta @ mage_glob[ni]
                pos = (MB @ np.c_[pos, np.ones(len(pos))].T).T[:, :3]
                nrm = (MB[:3, :3] @ nrm.T).T
            nrm = nrm / (np.linalg.norm(nrm, axis=1, keepdims=True) + 1e-9)
            idx = accessor(mage, mbin, prm["indices"]).reshape(-1)
            P.append(pos); NN.append(nrm); UV.append(uv); IDX.append(idx + voff); voff += len(pos)
    return (np.concatenate(P).astype("<f4"), np.concatenate(NN).astype("<f4"),
            np.concatenate(UV).astype("<f4"), np.concatenate(IDX).astype("<u4"))


def main():
    a = sys.argv
    mesh_path, out = a[1], a[2]
    anim_path = a[a.index("--anim") + 1] if "--anim" in a else None
    clip_name = a[a.index("--clip") + 1] if "--clip" in a else None
    frames = int(a[a.index("--frames") + 1]) if "--frames" in a else 1

    mage, mbin = read_glb(mesh_path)
    img = mage["images"][0]; ibv = mage["bufferViews"][img["bufferView"]]
    png = mbin[ibv.get("byteOffset", 0): ibv.get("byteOffset", 0) + ibv["byteLength"]]
    mime = img.get("mimeType", "image/png")

    anim = abin = clip = ch = None
    if anim_path:
        anim, abin = read_glb(anim_path)
        clip = next(c for c in anim["animations"] if c["name"] == clip_name)
        ch = {}
        for c in clip["channels"]:
            tgt = c["target"]
            if "node" in tgt:
                ch.setdefault(tgt["node"], {})[tgt["path"]] = c["sampler"]
        dur = max(accessor(anim, abin, s["input"]).max() for s in clip["samplers"])

    if frames == 1:
        t = (dur * 0.0) if clip else 0.0
        P, N, UV, IDX = bake_frame(mage, mbin, anim, abin, clip, ch, t)
        write_glb(out, P, N, UV, IDX, png, mime)
        print(f"baked {len(P)} verts, {len(IDX)//3} tris -> {out}  Y[{P[:,1].min():.2f},{P[:,1].max():.2f}]")
    else:
        stem = out[:-4] if out.endswith(".glb") else out
        for fr in range(frames):
            t = dur * fr / frames  # one loop, last frame != first so it cycles
            P, N, UV, IDX = bake_frame(mage, mbin, anim, abin, clip, ch, t)
            write_glb(f"{stem}_{fr:02d}.glb", P, N, UV, IDX, png, mime)
        print(f"baked {frames} frames of '{clip_name}' ({dur:.2f}s) -> {stem}_00..{frames-1:02d}.glb")


if __name__ == "__main__":
    main()
