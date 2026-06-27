#pragma once

/**
 * @file DayNightCycle.hpp
 * @brief Automatic day/night cycle driving the sun/moon light, sky tint and
 *        shadow direction.
 */

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

/**
 * @brief Continuous, automatic day/night cycle.
 *
 * Header-only and free of any Vulkan resources (same convention as the other
 * src/ modules). It advances a normalized clock on its own every frame there
 * is deliberately no way for the player to pause or scrub it and derives the
 * sun/moon direction, colour and brightness from that clock. The resulting
 * State drives the directional light in the main shader, the skybox tint, and
 * the sun's shadow map.
 */
class DayNightCycle {
public:
  /** @brief Sampled lighting state for the current instant of the cycle. */
  struct State {
    glm::vec3 sunDir;   /** Direction the sunlight travels (points to the ground).Useful for shaders */
    glm::vec3 toSun;    /** Unit vector from the scene toward the sun (for the sky disc). Useful for skybox */
    glm::vec3 color;    /** Sun (day) or moon (night) colour. */
    float intensity;    /** Directional light brightness; ~0 deep at night. */
    float dayFactor;    /** 0 at night, 1 in full day; used to blend sky/ambient. */
  };

  /**
   * @brief Advances the clock by one frame and returns the sampled state.
   *
   * @param deltaT Frame time in seconds.
   */
  State update(float deltaT) {
    timeOfDay += deltaT * SPEED;
    timeOfDay -= std::floor(timeOfDay); // wrap to [0,1)
    return sample();
  }

  /** @brief Current clock position in [0,1): 0 = midnight, 0.5 = noon. */
  float clock() const { return timeOfDay; }

private:
  // One full day every 120 s. Continuous and fixed: the cycle is part of the
  // ambience, not something the player drives.
  static constexpr float SPEED = 1.0f / 120.0f;

  // timeOfDay in [0,1): 0.0 = midnight, 0.25 = sunrise, 0.5 = noon, 0.75 = sunset.
  // Starts mid-morning so the scene opens comfortably lit.
  float timeOfDay = 0.30f;

  /** @brief Maps the current clock to a lighting State. */
  State sample() const {
    // The sun sweeps a half-circle above the horizon between sunrise and sunset.
    // phase = 0 at sunrise, pi/2 at noon, pi at sunset; elevation = sin(phase)
    // so it is 0 on the horizon, 1 overhead, and negative through the night.
    const float phase = (timeOfDay - 0.25f) * glm::two_pi<float>(); //Xonverts [0,1) to (-pi/2 , 3pi/2)
    const float elevation = std::sin(phase); //Sun height

    // East (-X) at dawn, overhead at noon, west (+X) at dusk, with a slight
    // constant tilt on Z so the arc is not a dead-vertical plane.
    glm::vec3 toSun = glm::normalize(glm::vec3(-std::cos(phase), elevation, 0.20f));

    // Fade the directional light in as the sun clears the horizon and out as it
    // sets; dayMix additionally separates low warm sun from high white daylight.
    // The fade starts exactly at the horizon (elevation 0), never below it: a sun
    // with elevation < 0 would shine *upward* through the ground plane, unshadowed
    // (its shadow strength is also 0 there), producing a flash as it crosses the
    // horizon at sunrise/sunset. Keeping it above 0 ties intensity and shadow
    // together so the transition is smooth.
    const float aboveFade = smoothstep(0.0f, 0.15f, elevation);
    // Slow ramp so the yellow-orange tint dominates most of the arc, only giving
    // way to white-yellow once the sun is high overhead.
    const float dayMix = smoothstep(0.0f, 0.75f, elevation);

    const glm::vec3 warmSun = glm::vec3(1.00f, 0.45f, 0.05f); // low sun: deep amber-orange
    const glm::vec3 noonSun = glm::vec3(1.00f, 0.95f, 0.72f); // zenith: warm white-yellow
    const glm::vec3 moon    = glm::vec3(0.45f, 0.55f, 0.85f); // night: cool blue-white

    State s{};
    s.toSun = toSun;
    s.sunDir = -toSun;
    s.color = glm::mix(moon, glm::mix(warmSun, noonSun, dayMix), aboveFade);
    // No exterior directional light at night: the moon casts nothing, so the only
    // illumination outdoors comes from the tavern's own torches spilling through
    // its windows and doorway (their point lights already reach outside). Daytime
    // ramps from a warm low sun to the midday sun. A directional light has no
    // distance attenuation, so it must stay on the same scale as the torches'
    // point lights (intensity ~0.6-1.0) or it floods the whole interior.
    s.intensity = glm::mix(0.0f, glm::mix(0.45f, 1.2f, dayMix), aboveFade);
    s.dayFactor = aboveFade;
    return s;
  }
  /**
   * @brief Hermite (smoothstep) interpolation, clamped to [0,1].
   * @param edge0 Lower edge (returns 0 at/below it).
   * @param edge1 Upper edge (returns 1 at/above it).
   * @param x Value to map.
   * @return Smoothly ramped value in [0,1].
   */
  static float smoothstep(float edge0, float edge1, float x) {
    float t = glm::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
  }
};
