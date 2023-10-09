const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const minim = b.addExecutable(.{
        .name = "minim",
        .target = target,
        .optimize = optimize,
        .root_source_file = .{ .path = "src/main.zig" },
    });
    b.installArtifact(minim);

    const run_minim = b.addRunArtifact(minim);
    if (b.args) |args| {
        run_minim.addArgs(args);
    }
    b.step("run", "Run minim").dependOn(&run_minim.step);
}
