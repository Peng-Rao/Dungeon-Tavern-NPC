"""Build the Dungeon-Tavern scene in Blender from scene.json (+ corridor.json).

Run inside Blender:  exec(open("/Users/pengrao/Workspace/Dungeon-Tavern-NPC/blender/build_scene.py").read())

Coordinate mapping (engine glTF Y-up -> Blender Z-up, matching the glTF importer):
  pos [x, y, z] -> location (x, -z, y)
  yaw degrees about +Y -> rotation about +Z
"""

import json
import math
import os

import bpy
from mathutils import Vector

PROJECT = "/Users/pengrao/Workspace/Dungeon-Tavern-NPC"
SOURCE = os.path.join(PROJECT, "source")
SCENE_JSON = os.path.join(SOURCE, "assets", "scenes", "scene.json")
CORRIDOR_JSON = os.path.join(PROJECT, "blender", "corridor.json")


def clear_scene():
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    for block_list in (bpy.data.meshes, bpy.data.materials, bpy.data.images,
                       bpy.data.actions, bpy.data.armatures, bpy.data.collections):
        for block in list(block_list):
            if block.users == 0:
                block_list.remove(block)


def get_collection(name, parent):
    col = bpy.data.collections.get(name)
    if col is None:
        col = bpy.data.collections.new(name)
        parent.children.link(col)
    return col


_asset_cache = {}


def get_asset_collection(model_rel, lib_col):
    """Import a glTF once and keep it in a hidden library collection."""
    if model_rel in _asset_cache:
        return _asset_cache[model_rel]
    path = os.path.join(SOURCE, model_rel)
    name = os.path.splitext(os.path.basename(model_rel))[0]
    col = bpy.data.collections.new("asset_" + name)
    lib_col.children.link(col)
    prev = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    for obj in [o for o in bpy.data.objects if o not in prev]:
        for user_col in list(obj.users_collection):
            user_col.objects.unlink(obj)
        col.objects.link(obj)
    _asset_cache[model_rel] = col
    return col


def add_flame_light(name, location, energy, radius, target_col):
    light_data = bpy.data.lights.new(name, type='POINT')
    light_data.color = (1.0, 0.55, 0.25)
    light_data.energy = energy
    light_data.shadow_soft_size = radius
    light = bpy.data.objects.new(name, light_data)
    light.location = location
    target_col.objects.link(light)


def place(entry, target_col, lib_col):
    col = get_asset_collection(entry["model"], lib_col)
    name = os.path.splitext(os.path.basename(entry["model"]))[0]
    inst = bpy.data.objects.new(name, None)
    inst.instance_type = 'COLLECTION'
    inst.instance_collection = col
    inst.empty_display_size = 0.25
    p = entry["pos"]
    inst.location = (p[0], -p[2], p[1])
    inst.rotation_euler = (0.0, 0.0, math.radians(entry.get("yaw", 0.0)))
    s = entry.get("scale", 1.0)
    inst.scale = (s, s, s)
    target_col.objects.link(inst)

    # Mirror the engine: *_lit models get a warm point light at the flame.
    if name == "torch_lit":
        add_flame_light(name + "_flame", Vector(inst.location) + Vector((0, 0, 0.55)),
                        energy=60.0, radius=0.08, target_col=target_col)
    elif name in ("candle_lit", "candle_thin_lit"):
        add_flame_light(name + "_flame", Vector(inst.location) + Vector((0, 0, 0.18)),
                        energy=8.0, radius=0.03, target_col=target_col)


def build(json_path, col_name):
    scene_col = bpy.context.scene.collection
    lib_col = get_collection("AssetLibrary", scene_col)
    target_col = get_collection(col_name, scene_col)
    with open(json_path) as f:
        data = json.load(f)
    for entry in data["objects"]:
        place(entry, target_col, lib_col)
    # Keep the import library out of the view layer.
    for child in bpy.context.view_layer.layer_collection.children:
        if child.name == "AssetLibrary":
            child.exclude = True


def setup_world_and_camera():
    world = bpy.data.worlds.get("World") or bpy.data.worlds.new("World")
    bpy.context.scene.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes.get("Background")
    if bg:
        bg.inputs[0].default_value = (0.015, 0.015, 0.02, 1.0)
        bg.inputs[1].default_value = 1.0

    cam_data = bpy.data.cameras.new("CorridorCam")
    cam = bpy.data.objects.new("CorridorCam", cam_data)
    cam.location = (-3.0, 0.0, 2.0)
    direction = Vector((-20.0, 0.0, 1.4)) - cam.location
    cam.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()
    bpy.context.scene.collection.objects.link(cam)
    bpy.context.scene.camera = cam


if __name__ == "__main__":
    # corridor.json was merged into scene.json once the corridor shipped to the
    # game; the engine's scene.json is now the single source of truth.
    clear_scene()
    build(SCENE_JSON, "Tavern")
    setup_world_and_camera()
    print("Scene built: %d objects" % len(bpy.data.objects))
