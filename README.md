#pci-chardev#

A generic driver for reading and writing PCI(e) BARs via character device files.

I use it for fast prototyping of early FPGA PCI(e) designs, because I can conveniently
read and write from the BARs via scripting languages.
Included is an example for accessing the character devices with Ruby.

The driver is basically a crossover of Linux' msr.c and pci-stub.c
and borrows some code from them.

##usage##

You can dynamically add PCI(e) devices like it is done for pci-stub, e.g.:

```shell
echo "10ee 7014"  > /sys/bus/pci/drivers/pci_char/new_id 
echo 0000:20:00.0 > /sys/bus/pci/devices/0000\:20\:00.0/driver/unbind
echo 0000:20:00.0 > /sys/bus/pci/drivers/pci_char/bind
```

Or via parameter at module probing, e.g.:

```shell
insmod pci-char ids=10ee:7014
```

The driver creates a character device file for __each memory BAR__ it finds on the PCI(e) device, e.g.:

```shell
/dev/pci-char/01:00.01/bar0
/dev/pci-char/01:00.01/bar3
```

You can read from and write to these files in 32bit
chunks, aka 4byte aligned. Accessing memory addresses
within the bar is realized by setting an offset into
the file via the (l)lseek() system call.

##reading / writing with supplied Ruby script##

An example ruby script is included which you can use for reading/writing.

Usage:
```shell
./pci-char.rb /dev/pci-char/bb:dd.f/barX address [new value]`
```

Example read:
```shell
./pci-char.rb /dev/pci-char/01\:00.01/bar3 0x0
```

Example write:
```shell
./pci-char.rb /dev/pci-char/01\:00.01/bar3 0x0 0xcafe
```

##License##
Copyright (C) 2012-2014  Andre Richter

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.


