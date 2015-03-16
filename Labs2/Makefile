KDIR=/lib/modules/`uname -r`/build

kbuild:
	make -C $(KDIR) M=`pwd`

install:
	make -C $(KDIR) M=`pwd` modules_install

clean:
	make -C $(KDIR) M=`pwd` clean
