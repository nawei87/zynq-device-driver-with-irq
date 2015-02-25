// Zynq�œ��삷��Linux�����́A�ȒP�ȃf�o�C�X�h���C�o�ł��B
// ����d�q��H��Cosmo-Z��ADigilent��ZedBoard�œ�����m�F���Ă���܂��B
// author	: Hideyuki Kimura
// license 	: GPL 
//
// �ҏW����
// 20150218		mknod�����������邽�߂ɁAclass_create()/device_create()�Ȃǂ�ǉ�
// 20150225		main()�Ahello_fasync()�ł̕Ԃ�l���Ȃ������̔������C���Bhello_fasync()����fasync_helper()���ĂԑO��magic�����Ȃ��Bfasync��release����
//
// TODO :
// �E�f�o�C�X�X�y�V�����t�@�C����chmod(���Ȃ��Ƃ������̂ŕK�v�Ȃ���������Ȃ��B)

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>		// class_create
#include <linux/sched.h>
#include <linux/interrupt.h>	// request_irq/free_irq
#include <linux/jiffies.h>		// jiffies
#include <linux/cdev.h>			// cdev_init/cdev_add/cdev_del
#include <linux/slab.h>			// kmalloc
#include "fpga_reg_util.h"


MODULE_AUTHOR("H.Kimura");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("adcdriver"); 

// constant variables
static char* DEV_NAME = "adcdriver";
// const int default_irq_number = 86;
static int irq_number = 86;						// my IRQ number. default IRQ number is #86. 

// global variables
dev_t allocated_device_number;
const unsigned int alloc_count = 1;				// �f�o�C�X���B��������ꍇ�����̃R�[�h�͂܂��z�肵�ĂȂ��B
struct class* adc_class;
struct device* adc_device;
struct cdev *adc_cdev;
struct fasync_struct *p_fasync_struct = NULL;	// �|�C���^�̎��͎̂��B�|�C���^�̎Q�Ɛ�͂Ȃ��B(fasync_helper�Ŏ����I�Ɋm�ۂ����)

module_param(irq_number, int, S_IRUGO);			// You can specify this when to start the driver.

static int hello_open(struct inode* inode, struct file* pfile)
{
	printk(KERN_INFO "%s(%pF) called at process %i (%s) \r\n", __func__, &hello_open, current->pid, current->comm);
	return 0;
}

static ssize_t hello_read(struct file* pfile, char* buf, size_t count, loff_t* pos)
{
	printk(KERN_INFO "%s(%pF) called at process %i (%s) \r\n", __func__, &hello_read, current->pid, current->comm);
	return 0;
}

static ssize_t hello_write(struct file* pfile, const char* buf, size_t count, loff_t* pos)
{
	printk(KERN_INFO "%s(%pF) called at process %i (%s) \r\n", __func__, &hello_write, current->pid, current->comm);

	return count;
}

static int hello_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
	printk(KERN_INFO "%s(%pF) called at process %i (%s) \r\n", __func__, &hello_ioctl, current->pid, current->comm);

	switch (cmd){
	default:
		return -ENOTTY;
	}

	return 0;
}

static int hello_fasync(int fd, struct file *filp, int mode)
{
	printk(KERN_INFO "%s(%pF) called at process %i (%s) \r\n", __func__, &hello_fasync, current->pid, current->comm);

	const int ret_val = fasync_helper(fd, filp, mode, &p_fasync_struct);
	if(ret_val < 0){
		// error
		printk(KERN_WARNING "failed at fasync_helper()\r\n");
	} else if(ret_val == 0){
		printk(KERN_NOTICE "use old fasync_struct. no struct inserted to async list.\r\n");
	} else {
		printk(KERN_NOTICE "new fasync_struct allocated.\r\n");
	}

	return 0;		// 2015/02/25
}

static int hello_release(struct inode* inode, struct file* pfile)
{
	printk(KERN_INFO "%s(%pF) called at process %i (%s) \r\n", __func__, &hello_release, current->pid, current->comm);

	// ������fasync��flag�������B(2015/02/25)
	const int ret_val = hello_fasync(-1, pfile, 0);

	return ret_val;
}

static struct file_operations hello_fops = 
{
	.owner			= THIS_MODULE,
	.read			= hello_read,
	.write			= hello_write,
	.open			= hello_open,
	.release		= hello_release,
	.unlocked_ioctl	= hello_ioctl,
	.fasync			= hello_fasync,
};


// Interrupt handler
static irqreturn_t test_interrupt_handler(int received_irq, void* dev_id)
{
	// ���L�\�ȃh���C�o�Ƃ��邽�߂ɁA�����̃h���C�o�ȊO�̊����݂Ȃ�΁A��������IRQ_NONE��Ԃ��B

	if (received_irq == irq_number){
		// ���荞�݃n���h���͋ɗ͑����������ς܂���ׂ��B���͖��Ȃ����Aprintk�����܂�ǂ��Ȃ��B
		printk("IRQ %d interrupted! dev_id \"%s\", jiffies %ld\r\n", received_irq, (char*)dev_id, jiffies);

		// �����݂�������A�܂��͊����݃��W�X�^�̒��g������B
		const unsigned int reg_pushsw_int = read_fpga_register(0x20018 * sizeof(unsigned long));

		bool is_interrupt_from_adc = false;

		// �ǂ̃v�b�V���X�C�b�`����̊����݂��m�F����B
		if ((reg_pushsw_int & (1 << 0)) == (1 << 0)){
			printk(KERN_INFO "interrupt by pushsw(1)\r\n");	// SW1
		} else if ((reg_pushsw_int & (1 << 2)) == (1 << 2)){
			printk(KERN_INFO "interrupt by pushsw(2)\r\n");	// SW2
		} else if ((reg_pushsw_int & (1 << 4)) == (1 << 4)){
			printk(KERN_INFO "interrupt by pushsw(3)\r\n");	// SW3
		} else {
			printk(KERN_INFO "interrupted by ADC\r\n", reg_pushsw_int);	// ADC
			is_interrupt_from_adc = true;
		}

		// ���荞�݂�������A���荞�݃��W�X�^�̊��荞�݃t���O��'0'�ɂ��āA���荞�݂��Ȃ����B
		if (is_interrupt_from_adc){
			write_fpga_register(0x20019 * sizeof(unsigned long), (1 << 1));
			printk(KERN_INFO "%x : %08x\r\n", 0x20019 * sizeof(unsigned long), read_fpga_register(0x20019 * sizeof(unsigned long)));
		}
		else {
			write_fpga_register(0x20018 * sizeof(unsigned long), (1 << 1) | (1 << 3) | (1 << 5));
			printk(KERN_INFO "%x : %08x\r\n", 0x20018 * sizeof(unsigned long), read_fpga_register(0x20018 * sizeof(unsigned long)));
		}

		// �����݂��N�����̂ŁA���[�U�[�v���Z�X�ɒʒm�𑗂�
		if (p_fasync_struct){
			// magic��\������(�f�o�b�O)
			// printk(KERN_INFO "magic : %d\r\n", p_fasync_struct->magic);

			kill_fasync(&p_fasync_struct, SIGIO, POLL_IN);
		}

		return IRQ_HANDLED;
	} else {
		return IRQ_NONE;
	}
}

static int __init hello_init(void)
{
	// �f�o�C�X�ԍ��̊��蓖��(���I)
	const int result = alloc_chrdev_region(&allocated_device_number, 0, alloc_count, DEV_NAME);

	if (result < 0){
		printk(KERN_WARNING "can't get major number %d\r\n", MAJOR(allocated_device_number));
		return result;
	}

	// �����I��mknod����(2015/02/18)
	adc_class = class_create(THIS_MODULE, DEV_NAME);
	if(IS_ERR(adc_class)){
		printk(KERN_WARNING "failed at class_create()\r\n");

		unregister_chrdev_region(allocated_device_number, alloc_count);
		
		return -1;
	}

	// mknod�̂��߂̃f�o�C�X(2015/02/18)
	adc_device = device_create(adc_class, NULL, allocated_device_number, NULL, DEV_NAME);
	if(IS_ERR(adc_device)){
		printk(KERN_WARNING "failed at device_create()\r\n");
		
		class_destroy(adc_class);
		unregister_chrdev_region(allocated_device_number, alloc_count);
		
		return -1;
	}

	// �h���C�o��o�^����Bcdev_init()�łȂ��Ƃ��悢�B
	// �G���[�`�F�b�N�ǉ�(2015/02/18)
	adc_cdev = cdev_alloc();
	if(adc_cdev == NULL){
		printk(KERN_WARNING "failed at cdev_alloc()\r\n");
		
		class_destroy(adc_class);
		unregister_chrdev_region(allocated_device_number, alloc_count);
		
		return -1;
	}

	adc_cdev->ops	= &hello_fops;
	adc_cdev->owner	= THIS_MODULE;

	const int err_code = cdev_add(adc_cdev, allocated_device_number, 1);
	if (err_code){
		printk(KERN_WARNING "Error %d adding %s\r\n", err_code, DEV_NAME);

		device_destroy(adc_class, allocated_device_number);
		class_destroy(adc_class);
		unregister_chrdev_region(allocated_device_number, alloc_count);

		return -EBUSY;
	} else {
		printk(KERN_NOTICE "We successfully added this device as %d %d\r\n", MAJOR(allocated_device_number), MINOR(allocated_device_number));
	}

	// ���荞�݃n���h���̓o�^
	printk(KERN_INFO "install %s into irq %d\n", DEV_NAME, irq_number);

	// SA_SHIRQ �� IRQF_SHARED�ASA_INTERRUPT �� IRQF_DISABLED
	if (request_irq(irq_number, test_interrupt_handler, IRQF_SHARED, DEV_NAME, DEV_NAME))
	{		
		// �o�^�ł��Ȃ����̏I������
		printk(KERN_WARNING "failed at request_irq(), unregister driver (devnum : %d, DEV_NAME : %s)\r\n", allocated_device_number, DEV_NAME);

		cdev_del(adc_cdev);
		device_destroy(adc_class, allocated_device_number);
		class_destroy(adc_class);
		unregister_chrdev_region(allocated_device_number, alloc_count);
		
		return -EBUSY;
	}

	// ���荞�݃��W�X�^�̊��荞�݋��t���O(INT_EN)�𗧂ĂāA�����߂�悤�ɂ��Ă����B
	map_fpga_register(XPAR_AXI_EXT_SLAVE_CONN_0_S_AXI_RNG00_BASEADDR, 0x20100 * sizeof(unsigned long));
	
	write_fpga_register(0x20018 * sizeof(unsigned long), ((1 << 1) | (1 << 3) | (1 << 5)));			// 0x2a
	write_fpga_register(0x20019 * sizeof(unsigned long), (1 << 1));
	print_fpga_registers();

	// �񓯊��ʒm�p�̍\���̂̊m��(�����ł��K�v����H)
	// p_fasync_struct = NULL;	// (struct fasync_struct*)kmalloc(sizeof(struct fasync_struct), GFP_ATOMIC);
	// if(p_fasync_struct == NULL){
	// 	printk(KERN_WARNING "failed at kmalloc()\r\n");

	// 	free_irq(irq_number, DEV_NAME);
	// 	cdev_del(adc_cdev);
	// 	device_destroy(adc_class, allocated_device_number);
	// 	class_destroy(adc_class);
	// 	unregister_chrdev_region(allocated_device_number, alloc_count);

	// 	return -ENOMEM;
	// } else {
	// 	// memset(p_fasync_struct, 0, sizeof(struct fasync_struct));		// Zero clear
	// 	// p_fasync_struct->magic = 

	// 	// 2015/01/13
	// 	// magic��\������(�f�o�b�O)
	// 	// printk(KERN_INFO "magic : %d\r\n", p_fasync_struct->magic);
	// 	// printk(KERN_INFO "Address of p_fasync_struct is %x\r\n", &p_fasync_struct);
	// }
	return 0;
}

static void __exit hello_exit(void)
{
	// �����݃n���h���̓o�^����������B
	printk(KERN_INFO "removing %s from irq %d\n", DEV_NAME, irq_number);
	free_irq(irq_number, DEV_NAME);

	// ���荞�ނ��Ƃ��Ȃ��̂ŁA�����݋��t���O��0�ɂ���B
	write_fpga_register(0x20018 * sizeof(unsigned long), 0x00000000);
	write_fpga_register(0x20019 * sizeof(unsigned long), 0x00000000);
	unmap_fpga_register(XPAR_AXI_EXT_SLAVE_CONN_0_S_AXI_RNG00_BASEADDR, 0x20100 * sizeof(unsigned long));

	// �񓯊��ʒm�p�\���̂̊m�ۂ��Ă����������J��
	// kfree(p_fasync_struct);
	
	// �h���C�o�̓o�^����������B
	cdev_del(adc_cdev);

	// mknod�̂��߂ɍ쐬����class������
	device_destroy(adc_class, allocated_device_number);
	class_destroy(adc_class);

	// �f�o�C�X�̓o�^�������B(2015/02/25)
	unregister_chrdev_region(allocated_device_number, alloc_count);

	printk(KERN_INFO "We successfully unregisterd driver (DEV_NAME : %s)\n", DEV_NAME);
}

module_init(hello_init);
module_exit(hello_exit);