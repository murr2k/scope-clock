"""
PlatformIO pre-script: add link-time flags that build_flags cannot express.

  --gc-sections   pairs with -ffunction-sections/-fdata-sections to drop code
                  and data nothing references.  libopencm3 compiles its whole
                  USB stack, FDCAN, QUADSPI etc. into the archive; without this
                  their static buffers land in .bss and the G431's 32 KB SRAM
                  overflows.

Note: -specs=nano.specs is NOT added here.  PlatformIO already forwards
build_flags to LINKFLAGS, so adding it again makes gcc fail with
"attempt to rename spec 'link' to already defined spec 'nano_link'".

Import("env") is injected by PlatformIO's SCons build.
"""
Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

env.Append(LINKFLAGS=[
    "-Wl,--gc-sections",
    # A map file is worth keeping on a 32 KB part: it is the only way to see
    # who actually owns .bss when the link overflows.
    "-Wl,-Map,%s" % env.subst("$BUILD_DIR/firmware.map"),
])
