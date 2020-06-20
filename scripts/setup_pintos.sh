#! /bin/sh

# pulls pintos source and performs various setup
# call this script from root directory
# WARNING: this will overwrite existing pintos directory


# get the pintos source
wget http://www.stanford.edu/class/cs140/projects/pintos/pintos.tar.gz
tar -xzf pintos.tar.gz
mv pintos/src tmp
rm -r pintos pintos.tar.gz
mv tmp pintos


# pintos reconfiguration taken from https://github.com/JohnStarich/docker-pintos/blob/master/Dockerfile

# Fix ACPI bug
## Fix described here under "Troubleshooting": http://arpith.xyz/2016/01/getting-started-with-pintos/
sed -i '/serial_flush ();/a \
  outw( 0x604, 0x0 | 0x2000 );' ${1}/devices/shutdown.c

# Configure Pintos for QEMU
sed -i 's/bochs/qemu/' ${1}/*/Make.vars
## Reconfigure Pintos to use QEMU
sed -i 's/\/usr\/class\/cs140\/pintos\/pintos\/src/\/pintos/' ${1}/utils/pintos-gdb && \
    sed -i 's/LDFLAGS/LDLIBS/' ${1}/utils/Makefile && \
    sed -i 's/\$sim = "bochs"/$sim = "qemu"/' ${1}/utils/pintos && \
    sed -i 's/kernel.bin/\/pintos\/threads\/build\/kernel.bin/' ${1}/utils/pintos && \
    sed -i "s/my (@cmd) = ('qemu');/my (@cmd) = ('qemu-system-x86_64');/" ${1}/utils/pintos && \
    sed -i 's/loader.bin/\/pintos\/threads\/build\/loader.bin/' ${1}/utils/Pintos.pm
