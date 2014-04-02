CC = gcc
obj-m += pci-char.o

pci_char:
	@echo "********************************"
	@echo "* Compiling                    *"
	@echo "********************************"
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
