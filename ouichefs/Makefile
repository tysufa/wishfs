obj-m += ouichefs.o
ouichefs-objs := fs.o super.o inode.o file.o dir.o

KERNELDIR ?= /lib/modules/$(shell uname -r)/build

depsdir=deps

all: $(depsdir)
	make -C $(KERNELDIR) M=$(depsdir) modules

debug: $(depsdir)
	make -C $(KERNELDIR) M=$(depsdir) ccflags-y+="-DDEBUG -g" modules

clean: $(depsdir)
	$(MAKE) -C $(KERNELDIR) M=$(depsdir) clean
	rm -rf *~
	rm -rf $(depsdir)
	$(MAKE) -C ./mkfs mrproper

$(depsdir):
	mkdir -p $(depsdir)

.PHONY: all clean
