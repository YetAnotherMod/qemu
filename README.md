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

MB115.01 and MT174.04 boards are available with PowerPC 476FP core.

To start MB115.01 you can use following command:

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

To start MT174.04 you can use following command:

```bash
    sudo ./qemu-system-ppc \
        -M mt174.04 \
        -bios ../pc-bios/module_mt174_rumboot.bin \
        -monitor tcp::2345,server,nowait \
        -serial tcp::3555,server,nodelay,nowait \
        -gdb tcp::1234,server,nowait
```

If you want to launch qemu with SD card use additional argument:

```bash
    -drive file=*path_to_sd_image_file*,if=sd,format=raw
```


Original QEMU readme is renamed to [README_original.rst](README_original.rst)
