# QEMU with PowerPC 476FP support

## Building

The simple steps to build QEMU for PowePC are:

```bash
    mkdir build
    cd build
    ../configure --target-list=ppc-softmmu \
        --disable-vnc --disable-sdl --disable-gnutls --disable-nettle --disable-gtk
    make
```

## Launching

PowerPC 476FP SoC and board availability:

|Board name|System on Chip used|Firmware name|
|:------:|:------:|:------:|
|mb115.01|1888TX018|module_mb115_rumboot.bin|
|mt150.02|1888BM018|module_mt150_rumboot.bin|
|mt174.04|1888BM028A|module_mt174_rumboot.bin|

To start MB115.01 board you can use following command:

```bash
    sudo ./qemu-system-ppc \
        -M mb115.01 \
        -bios ../pc-bios/module_mb115_rumboot.bin \
        -drive file=../pc-bios/module_mb115_u-boot.bin,if=mtd,format=raw \
        -monitor tcp::2345,server,nowait \
        -serial tcp::3555,server,nodelay,nowait \
        -gdb tcp::1234,server,nowait \
        -nic tap,model=greth,script=scripts/qemu-ifup,downscript=no
```

To start any other available board just replace `mb115.01` and `module_mb115_rumboot.bin` from the table above.

If you want to launch qemu with SD card use additional argument:

```bash
    -drive file=*path_to_sd_image_file*,if=sd,format=raw
```

If you want to change boot jumpers value (for example to set value `0x82`) use parameter `boot-cfg`:

```bash
    -M mb115.01,boot-cfg=0x82
```


Original QEMU readme is renamed to [README_original.rst](README_original.rst)
