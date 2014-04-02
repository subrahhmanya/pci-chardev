#!/usr/bin/env ruby
# -*- coding: utf-8 -*-
#
# ==========================================================
#
# Basic library for accessing pci-chardev devices.
# Reading/writing is realized with the (l)lseek system call
# and only works in 4 byte / 32 bit chunks
#
# Usage:
#  ./pci-char.rb /dev/pci-char/bb:dd.f/barX address [new value]
#
# Example read:
#  ./pci-char.rb /dev/pci-char/01\:00.01/bar3 0x0
#
# Example write:
#  ./pci-char.rb /dev/pci-char/01\:00.01/bar3 0x0 0xcafe
#
# Alternatively, you can include the file in your own
# scripts and use the read and write methods.
#
# ==========================================================
#
# Author(s):
#    Andre Richter, andre.o.richter @t gmail_com
#

module PCIChar

  def self.read(dev, addr)
    f = File.open(dev, "rb")
    f.seek(addr, IO::SEEK_SET)
    val = f.read(4).unpack("L")[0]
    f.close
    return val
  end

  def self.write(dev, addr, data)
    f = File.open(dev, "wb")
    f.seek(addr, IO::SEEK_SET)
    f.write([data].pack("L"))
    f.close
  end 

end

if __FILE__ == $0

  def arg_to_int(arg)
    arg = arg.gsub(/^0[xX]/, "")
    arg = arg[0..7]
    arg = arg[/^[[:xdigit:]]+/]

    return arg ? arg.to_i(16) : -1
  end

  if not File.chardev?(ARGV[0].dup)
    puts "not a chardev or module not loaded"
    exit
  end

  if (ARGV.length == 3)
    addr = arg_to_int(ARGV[1].dup)
    if (addr < 0)
      puts "first argument not a valid hex word"
      exit
    end

    data = arg_to_int(ARGV[2].dup)
    if (data < 0)
      puts "second argument not a valid hex word"
      exit
    end
    PCIChar::write(ARGV[0].dup, addr, data)

  elsif (ARGV.length == 2)
    addr = arg_to_int(ARGV[1].dup)
    if (addr < 0)
      puts "first argument not a valid hex word"
      exit
    end
    puts "0x%08x" % PCIChar::read(ARGV[0].dup, addr)

  else
    print "\nUsage: ./pcietools.rb /dev/pci-char/bb:dd.f/barX address [new value]\n"
    print "\taddress must be 4 Byte aligned and 4 Byte long.\n"
    print "\t[new value] must be 4 Byte long.\n\n"
    print  "Example: ./pci-char.rb 0x0 0x8\n\n"
  end

end
