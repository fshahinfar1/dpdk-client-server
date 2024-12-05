## Dependencies

**Install libraries**

```bash
sudo apt update
sudo apt install libnuma-dev
```

**Install RDMA core**

```bash
sudo apt update
sudo apt-get install -y build-essential cmake gcc libudev-dev libnl-3-dev libnl-route-3-dev ninja-build pkg-config valgrind python3-dev cython3 python3-docutils pandoc
git clone https://github.com/linux-rdma/rdma-core
cd rdma-core
cmake .
make -j
sudo make install
```


**Install DPDK**

```bash
export DPDK_INSTALL_DIR="$HOME/dpdk-build/"
sudo apt update
sudo apt-get install -y meson ninja-build python3-pip python3-pyelftools
wget http://fast.dpdk.org/rel/dpdk-22.11.2.tar.xz
tar -xf dpdk-22.11.2.tar.xz
cd dpdk-stable-22.11.2/
meson build
cd build
meson configure --prefix $DPDK_INSTALL_DIR
ninja
ninja install
echo $DPDK_INSTALL_DIR/lib/x86_64-linux-gnu/ | sudo tee /etc/ld.so.conf.d/dpdk_libs.conf
sudo ldconfig
echo export PKG_CONFIG_PATH=$DPDK_INSTALL_DIR/lib/x86_64-linux-gnu/pkgconfig | tee -a ~/.bashrc
```

**Configure Huge Page**

```
GRUB_CMDLINE_LINUX_DEFAULT="default_hugepagesz=1G hugepagesz=1G hugepages=16"
```

# NOTES

On Intel NICs you need to attach the driver. Use dpdk devbind scripts provided
with the DPDK. If you receive the following error, it might be due to IOMMU
configurations. Test the command below.

```
Error: bind failed for 0000:18:00.1 - Cannot bind to driver vfio-pci: [Errno 22] Invalid argument
```

```
 echo 1 | sudo tee  /sys/module/vfio/parameters/enable_unsafe_noiommu_mode
```
