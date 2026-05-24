#! /bin/bash

# Fix the paths if necessary
HDA="-drive file=pnl-tp-2021.img,format=raw"
HDB="-drive file=myHome.img,format=raw"
HDC="-drive file=test.img,format=raw"
SHARED="./share"

KERNEL_VERSION="6.5.7"

KERNEL="linux-${KERNEL_VERSION}/arch/x86/boot/bzImage"

printf '%s\n' "$KERNEL"

if [ -z ${KDB} ]; then
    CMDLINE='root=/dev/sda1 rw console=ttyS0 kgdboc=ttyS1'
else
    CMDLINE='root=/dev/sda1 rw console=ttyS0 kgdboc=ttyS1 kgdbwait'
fi

FLAGS="--enable-kvm "
VIRTFS+=" --virtfs local,path=${SHARED},mount_tag=share,security_model=passthrough,id=share "

exec qemu-system-x86_64 ${FLAGS} \
     ${HDA} ${HDB} ${HDC} \
     ${VIRTFS} \
     -net user -net nic \
     -serial mon:stdio -gdb tcp::1234,server,nowait \
     -boot c -m 1G \
     -kernel "${KERNEL}" \
     -append "${CMDLINE}"
