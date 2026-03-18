package(default_visibility = ["//visibility:public"])
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_library(
    name = "vulkan_headers",
    hdrs = glob(["include/vulkan/*.h", "include/vulkan/*.hpp"]),
    includes = ["include"],
)
