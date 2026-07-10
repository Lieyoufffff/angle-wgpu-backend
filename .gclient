solutions = [
  {
    "name": ".",
    "url": "https://chromium.googlesource.com/angle/angle.git",
    "deps_file": "DEPS",
    "managed": False,
    "custom_deps": {
      "third_party/SwiftShader": None,
      "third_party/VK-GL-CTS/src": None,
      "third_party/angle_restricted_traces": None,
    },
    "custom_vars": {
      "checkout_angle_dawn_deps": True,
      "checkout_angle_restricted_traces": False,
      "checkout_angle_internal": False,
      "checkout_angle_cl_deps": False,
      "checkout_angle_mesa": False,
    },
  },
]
