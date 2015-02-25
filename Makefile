# Linux kernel�̃\�[�X�R�[�h�̃p�X���w�肷��B�R���p�C���ɂ̓\�[�X�R�[�h���K�v�Ȃ̂ŁB
# for Ubuntu x64
# KERNEL_SRC=/lib/modules/${shell uname -r}/build

# for ARM(Zynq)
KERNEL_SRC=/home/tokuden/linux-digilent/

CFILES = hello.c fpga_reg_util.c

# �쐬���������W���[����
obj-m := driver.o

# <�쐬���������W���[����>-objs�ɁA���W���[�����\������I�u�W�F�N�g�̈ꗗ��񋓂���
driver-objs := hello.o fpga_reg_util.o

# Compile-time flags

all:
	make -C $(KERNEL_SRC) ARCH=arm M=$(PWD) V=1 modules
clean:
	make -C $(KERNEL_SRC) M=$(PWD) V=1 clean
