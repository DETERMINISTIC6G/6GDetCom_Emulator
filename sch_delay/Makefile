CONFIG_MODULE_SIG=n

obj-m += sch_delay.o

sch_delay-objs+= sch_delay_module.o sch_delay_chrdev.o 

all:
	mkdir -p ./build/
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	mv -t ./build/. sch_delay.* Module.symvers modules.order

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) O=$(PWD)/build/ clean

install:
	sudo cp ./build/sch_delay.ko /lib/modules/$(shell uname -r)/kernel/net/sch_delay.ko

uninstall:
	sudo rm /lib/modules/$(shell uname -r)/kernel/net/sch_delay.ko

load: 
	sudo sync
	sudo insmod build/sch_delay.ko

unload:
	sudo sync
	sudo rmmod sch_delay

reload:
	-make unload
	-make load

