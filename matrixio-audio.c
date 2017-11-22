/*
 * pisound Linux kernel module.
 * Copyright (C) 2016  Vilniaus Blokas UAB, http://blokas.io/pisound
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/sysfs.h>

#include <sound/asequencer.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
/*
static int pisnd_spi_init(struct device *dev);
static void pisnd_spi_uninit(void);

static void pisnd_spi_send(uint8_t val);
static uint8_t pisnd_spi_recv(uint8_t *buffer, uint8_t length);

typedef void (*pisnd_spi_recv_cb)(void *data);
static void pisnd_spi_set_callback(pisnd_spi_recv_cb cb, void *data);

static const char *pisnd_spi_get_serial(void);
static const char *pisnd_spi_get_id(void);
static const char *pisnd_spi_get_version(void);
*/
#define PISOUND_LOG_PREFIX "pisound: "

#ifdef DEBUG
#define printd(...) pr_alert(PISOUND_LOG_PREFIX __VA_ARGS__)
#else
#define printd(...)                                                            \
	do {                                                                   \
	} while (0)
#endif

#define printe(...) pr_err(PISOUND_LOG_PREFIX __VA_ARGS__)
#define printi(...) pr_info(PISOUND_LOG_PREFIX __VA_ARGS__)

static void *g_recvData;
// static pisnd_spi_recv_cb g_recvCallback;

#define FIFO_SIZE 512

static char g_serial_num[11];
static char g_id[25];
static char g_version[5];

static uint8_t g_ledFlashDuration;
static bool g_ledFlashDurationChanged;

DEFINE_KFIFO(spi_fifo_in, uint8_t, FIFO_SIZE);
DEFINE_KFIFO(spi_fifo_out, uint8_t, FIFO_SIZE);

static struct gpio_desc *data_available;
static struct gpio_desc *spi_reset;

static struct spi_device *pisnd_spi_device;

static struct workqueue_struct *pisnd_workqueue;
static struct work_struct pisnd_work_process;

static void pisnd_work_handler(struct work_struct *work);

// static uint16_t spi_transfer16(uint16_t val);

static int pisnd_init_workqueues(void)
{
	pisnd_workqueue = create_singlethread_workqueue("pisnd_workqueue");
	INIT_WORK(&pisnd_work_process, pisnd_work_handler);

	return 0;
}

static void pisnd_uninit_workqueues(void)
{
	flush_workqueue(pisnd_workqueue);
	destroy_workqueue(pisnd_workqueue);

	pisnd_workqueue = NULL;
}
/*
static bool pisnd_spi_has_more(void)
{
	return gpiod_get_value(data_available);
}
*/
enum task_e {
	TASK_PROCESS = 0,
};

static void pisnd_schedule_process(enum task_e task)
{
	/*
	if (pisnd_spi_device != NULL &&
		pisnd_workqueue != NULL &&
		!work_pending(&pisnd_work_process)
		) {
		printd("schedule: has more = %d\n", pisnd_spi_has_more());
		if (task == TASK_PROCESS)
			queue_work(pisnd_workqueue, &pisnd_work_process);
	}*/
}

static irqreturn_t data_available_interrupt_handler(int irq, void *dev_id)
{
	/*
	if (irq == gpiod_to_irq(data_available) && pisnd_spi_has_more()) {
		printd("schedule from irq\n");
		pisnd_schedule_process(TASK_PROCESS);
	}
*/
	return IRQ_HANDLED;
}

static DEFINE_SPINLOCK(spilock);
static unsigned long spilockflags;

/*
static uint16_t spi_transfer16(uint16_t val)
{
	int err;
	struct spi_transfer transfer;
	struct spi_message msg;
	uint8_t txbuf[2];
	uint8_t rxbuf[2];

	if (!pisnd_spi_device) {
		printe("pisnd_spi_device null, returning\n");
		return 0;
	}

	spi_message_init(&msg);

	memset(&transfer, 0, sizeof(transfer));
	memset(&rxbuf, 0, sizeof(rxbuf));

	txbuf[0] = val >> 8;
	txbuf[1] = val & 0xff;

	transfer.tx_buf = &txbuf;
	transfer.rx_buf = &rxbuf;
	transfer.len = sizeof(txbuf);
	transfer.speed_hz = 125000;
	transfer.delay_usecs = 100;
	spi_message_add_tail(&transfer, &msg);

	spin_lock_irqsave(&spilock, spilockflags);
	err = spi_sync(pisnd_spi_device, &msg);
	spin_unlock_irqrestore(&spilock, spilockflags);

	if (err < 0) {
		printe("spi_sync error %d\n", err);
		return 0;
	}

	printd("received: %02x%02x\n", rxbuf[0], rxbuf[1]);
	printd("hasMore %d\n", pisnd_spi_has_more());

	return (rxbuf[0] << 8) | rxbuf[1];
}

static int spi_read_bytes(char *dst, size_t length, uint8_t *bytesRead)
{
	uint16_t rx;
	uint8_t size;
	uint8_t i;

	memset(dst, 0, length);
	*bytesRead = 0;

	 rx = spi_transfer16(0);
	if (!(rx >> 8))
		return -EINVAL;

	size = rx & 0xff;

	if (size > length)
		return -EINVAL;

	for (i = 0; i < size; ++i) {
		rx = spi_transfer16(0);
		if (!(rx >> 8))
			return -EINVAL;

		dst[i] = rx & 0xff;
	}

	*bytesRead = i;

	return 0;
}

static int spi_device_match(struct device *dev, void *data)
{
	struct spi_device *spi = container_of(dev, struct spi_device, dev);

	printd("      %s %s %dkHz %d bits mode=0x%02X\n",
		spi->modalias, dev_name(dev), spi->max_speed_hz/1000,
		spi->bits_per_word, spi->mode);

	if (strcmp("pisound-spi", spi->modalias) == 0) {
		printi("\tFound!\n");
		return 1;
	}

	printe("\tNot found!\n");
	return 0;
}

static struct spi_device *pisnd_spi_find_device(void)
{
	struct device *dev;

	printi("Searching for spi device...\n");
	dev = bus_find_device(&spi_bus_type, NULL, NULL, spi_device_match);
	if (dev != NULL)
		return container_of(dev, struct spi_device, dev);
	else
		return NULL;
}
*/

static void pisnd_work_handler(struct work_struct *work)
{
	uint16_t rx;
	uint16_t tx;
	uint8_t val;
	/*
		if (work == &pisnd_work_process) {
			if (pisnd_spi_device == NULL)
				return;

			do {
				val = 0;
				tx = 0;

				if (g_ledFlashDurationChanged) {
					tx = 0xf000 | g_ledFlashDuration;
					g_ledFlashDuration = 0;
					g_ledFlashDurationChanged = false;
				} else if (kfifo_get(&spi_fifo_out, &val)) {
					tx = 0x0f00 | val;
				}

				rx = spi_transfer16(tx);

				if (rx & 0xff00) {
					kfifo_put(&spi_fifo_in, rx & 0xff);
					if (kfifo_len(&spi_fifo_in) > 16
						&& g_recvCallback)
						g_recvCallback(g_recvData);
				}
			} while (rx != 0
				|| !kfifo_is_empty(&spi_fifo_out)
				|| pisnd_spi_has_more()
				|| g_ledFlashDurationChanged
				);

			if (!kfifo_is_empty(&spi_fifo_in) && g_recvCallback)
				g_recvCallback(g_recvData);
		}
	*/
}

#if 0
static int pisnd_spi_gpio_init(struct device *dev)
{
	spi_reset = gpiod_get_index(dev, "reset", 1, GPIOD_ASIS);
	data_available = gpiod_get_index(dev, "data_available", 0, GPIOD_ASIS);

	gpiod_direction_output(spi_reset, 1);
	gpiod_direction_input(data_available);

	/* Reset the slave. */
	gpiod_set_value(spi_reset, false);
	mdelay(1);
	gpiod_set_value(spi_reset, true);

	/* Give time for spi slave to start. */
	mdelay(64);

	return 0;
}

static void pisnd_spi_gpio_uninit(void)
{
	gpiod_set_value(spi_reset, false);
	gpiod_put(spi_reset);
	spi_reset = NULL;

	gpiod_put(data_available);
	data_available = NULL;
}

static int pisnd_spi_gpio_irq_init(struct device *dev)
{
	return request_irq(
		gpiod_to_irq(data_available),
		data_available_interrupt_handler,
		IRQF_TIMER | IRQF_TRIGGER_RISING,
		"data_available_int",
		NULL
		);
}

static void pisnd_spi_gpio_irq_uninit(void)
{
	free_irq(gpiod_to_irq(data_available), NULL);
}

static int spi_read_info(void)
{
	uint16_t tmp;
	uint8_t count;
	uint8_t n;
	uint8_t i;
	uint8_t j;
	char buffer[257];
	int ret;
	char *p;

	memset(g_serial_num, 0, sizeof(g_serial_num));
	memset(g_version, 0, sizeof(g_version));
	memset(g_id, 0, sizeof(g_id));

	tmp = spi_transfer16(0);

	if (!(tmp >> 8))
		return -EINVAL;

	 count = tmp & 0xff;

	for (i = 0; i < count; ++i) {
		memset(buffer, 0, sizeof(buffer));
		ret = spi_read_bytes(buffer, sizeof(buffer)-1, &n);

		if (ret < 0)
			return ret;

		switch (i) {
		case 0:
			if (n != 2)
				return -EINVAL;

			snprintf(
				g_version,
				sizeof(g_version),
				"%x.%02x",
				buffer[0],
				buffer[1]
				);
			break;
		case 1:
			if (n >= sizeof(g_serial_num))
				return -EINVAL;

			memcpy(g_serial_num, buffer, sizeof(g_serial_num));
			break;
		case 2:
			{
				if (n >= sizeof(g_id))
					return -EINVAL;

				p = g_id;
				for (j = 0; j < n; ++j)
					p += sprintf(p, "%02x", buffer[j]);
			}
			break;
		default:
			break;
		}
	}

	return 0;
}

static int pisnd_spi_init(struct device *dev)
{
	int ret;
	struct spi_device *spi;

	memset(g_serial_num, 0, sizeof(g_serial_num));
	memset(g_id, 0, sizeof(g_id));
	memset(g_version, 0, sizeof(g_version));

	spi = pisnd_spi_find_device();

	if (spi != NULL) {
		printd("initializing spi!\n");
		pisnd_spi_device = spi;
		ret = spi_setup(pisnd_spi_device);
	} else {
		printe("SPI device not found, deferring!\n");
		return -EPROBE_DEFER;
	}

	ret = pisnd_spi_gpio_init(dev);

	if (ret < 0) {
		printe("SPI GPIO init failed: %d\n", ret);
		spi_dev_put(pisnd_spi_device);
		pisnd_spi_device = NULL;
		pisnd_spi_gpio_uninit();
		return ret;
	}

	ret = spi_read_info();

	if (ret < 0) {
		printe("Reading card info failed: %d\n", ret);
		spi_dev_put(pisnd_spi_device);
		pisnd_spi_device = NULL;
		pisnd_spi_gpio_uninit();
		return ret;
	}

	/* Flash the LEDs. */
	spi_transfer16(0xf008);

	ret = pisnd_spi_gpio_irq_init(dev);
	if (ret < 0) {
		printe("SPI irq request failed: %d\n", ret);
		spi_dev_put(pisnd_spi_device);
		pisnd_spi_device = NULL;
		pisnd_spi_gpio_irq_uninit();
		pisnd_spi_gpio_uninit();
	}

	ret = pisnd_init_workqueues();
	if (ret != 0) {
		printe("Workqueue initialization failed: %d\n", ret);
		spi_dev_put(pisnd_spi_device);
		pisnd_spi_device = NULL;
		pisnd_spi_gpio_irq_uninit();
		pisnd_spi_gpio_uninit();
		pisnd_uninit_workqueues();
		return ret;
	}

	if (pisnd_spi_has_more()) {
		printd("data is available, scheduling from init\n");
		pisnd_schedule_process(TASK_PROCESS);
	}

	return 0;
}

static void pisnd_spi_uninit(void)
{
	pisnd_uninit_workqueues();

	spi_dev_put(pisnd_spi_device);
	pisnd_spi_device = NULL;

	pisnd_spi_gpio_irq_uninit();
	pisnd_spi_gpio_uninit();
}

static void pisnd_spi_flash_leds(uint8_t duration)
{
	g_ledFlashDuration = duration;
	g_ledFlashDurationChanged = true;
	printd("schedule from spi_flash_leds\n");
	pisnd_schedule_process(TASK_PROCESS);
}

static void pisnd_spi_send(uint8_t val)
{
	kfifo_put(&spi_fifo_out, val);
	printd("schedule from spi_send\n");
	pisnd_schedule_process(TASK_PROCESS);
}

static uint8_t pisnd_spi_recv(uint8_t *buffer, uint8_t length)
{
	return kfifo_out(&spi_fifo_in, buffer, length);
}

static void pisnd_spi_set_callback(pisnd_spi_recv_cb cb, void *data)
{
	g_recvData = data;
	g_recvCallback = cb;
}

static const char *pisnd_spi_get_serial(void)
{
	if (strlen(g_serial_num))
		return g_serial_num;

	return "";
}

static const char *pisnd_spi_get_id(void)
{
	if (strlen(g_id))
		return g_id;

	return "";
}

static const char *pisnd_spi_get_version(void)
{
	if (strlen(g_version))
		return g_version;

	return "";
}
#endif

static const struct of_device_id pisound_of_match[] = {
    {
	.compatible = "blokaslabs,pisound",
    },
    {
	.compatible = "blokaslabs,pisound-spi",
    },
    {},
};

enum { SWITCH = 0,
       VOLUME = 1,
};

static int pisnd_ctl_info(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_info *uinfo)
{
	if (kcontrol->private_value == SWITCH) {
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
		uinfo->count = 1;
		uinfo->value.integer.min = 0;
		uinfo->value.integer.max = 1;
		return 0;
	} else if (kcontrol->private_value == VOLUME) {
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
		uinfo->count = 1;
		uinfo->value.integer.min = 0;
		uinfo->value.integer.max = 100;
		return 0;
	}
	return -EINVAL;
}

static int pisnd_ctl_get(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	if (kcontrol->private_value == SWITCH) {
		ucontrol->value.integer.value[0] = 1;
		return 0;
	} else if (kcontrol->private_value == VOLUME) {
		ucontrol->value.integer.value[0] = 100;
		return 0;
	}

	return -EINVAL;
}

static struct snd_kcontrol_new pisnd_ctl[] = {
    {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "PCM Playback Switch",
	.index = 0,
	.private_value = SWITCH,
	.access = SNDRV_CTL_ELEM_ACCESS_READ,
	.info = pisnd_ctl_info,
	.get = pisnd_ctl_get,
    },
    {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "PCM Playback Volume",
	.index = 0,
	.private_value = VOLUME,
	.access = SNDRV_CTL_ELEM_ACCESS_READ,
	.info = pisnd_ctl_info,
	.get = pisnd_ctl_get,
    },
};

static int pisnd_ctl_init(struct snd_card *card)
{
	int err, i;

	for (i = 0; i < ARRAY_SIZE(pisnd_ctl); ++i) {
		err = snd_ctl_add(card, snd_ctl_new1(&pisnd_ctl[i], NULL));
		if (err < 0)
			return err;
	}

	return 0;
}

static int pisnd_ctl_uninit(void) { return 0; }

static struct gpio_desc *osr0, *osr1, *osr2;
static struct gpio_desc *reset;
static struct gpio_desc *button;

static int pisnd_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;

	/* pisound runs on fixed 32 clock counts per channel,
	 * as generated by the master ADC.
	 */
	snd_soc_dai_set_bclk_ratio(cpu_dai, 32 * 2);

	printd("rate   = %d\n", params_rate(params));
	printd("ch     = %d\n", params_channels(params));
	printd("bits   = %u\n",
	       snd_pcm_format_physical_width(params_format(params)));
	printd("format = %d\n", params_format(params));

	gpiod_set_value(reset, false);

	switch (params_rate(params)) {
	case 48000:
		gpiod_set_value(osr0, true);
		gpiod_set_value(osr1, false);
		gpiod_set_value(osr2, false);
		break;
	case 96000:
		gpiod_set_value(osr0, true);
		gpiod_set_value(osr1, true);
		gpiod_set_value(osr2, false);
		break;
	case 192000:
		gpiod_set_value(osr0, true);
		gpiod_set_value(osr1, true);
		gpiod_set_value(osr2, true);
		break;
	default:
		printe("Unsupported rate %u!\n", params_rate(params));
		return -EINVAL;
	}

	gpiod_set_value(reset, true);

	return 0;
}

static unsigned int rates[3] = {48000, 96000, 192000};

static struct snd_pcm_hw_constraint_list constraints_rates = {
    .count = ARRAY_SIZE(rates), .list = rates, .mask = 0,
};

static int pisnd_startup(struct snd_pcm_substream *substream)
{
	int err = snd_pcm_hw_constraint_list(
	    substream->runtime, 0, SNDRV_PCM_HW_PARAM_RATE, &constraints_rates);

	if (err < 0)
		return err;

	err = snd_pcm_hw_constraint_single(substream->runtime,
					   SNDRV_PCM_HW_PARAM_CHANNELS, 2);

	if (err < 0)
		return err;

	err = snd_pcm_hw_constraint_mask64(
	    substream->runtime, SNDRV_PCM_HW_PARAM_FORMAT,
	    SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
		SNDRV_PCM_FMTBIT_S32_LE);

	if (err < 0)
		return err;

	return 0;
}

static struct snd_soc_ops pisnd_ops = {
    .startup = pisnd_startup, .hw_params = pisnd_hw_params,
};

static struct snd_soc_dai_link pisnd_dai[] = {
    {
	.name = "pisound",
	.stream_name = "pisound",
	.cpu_dai_name = "bcm2708-i2s.0",
	.codec_dai_name = "snd-soc-dummy-dai",
	.platform_name = "bcm2708-i2s.0",
	.codec_name = "snd-soc-dummy",
	.dai_fmt =
	    SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM,
	.ops = &pisnd_ops,
    },
};

static int pisnd_card_probe(struct snd_soc_card *card)
{
	int err;

	err = pisnd_ctl_init(card->snd_card);
	if (err < 0) {
		printe("pisnd_ctl_init failed: %d\n", err);
		return err;
	}

	return 0;
}

static int pisnd_card_remove(struct snd_soc_card *card)
{
	pisnd_ctl_uninit();
	return 0;
}

static struct snd_soc_card pisnd_card = {
    .name = "pisound",
    .owner = THIS_MODULE,
    .dai_link = pisnd_dai,
    .num_links = ARRAY_SIZE(pisnd_dai),
    .probe = pisnd_card_probe,
    .remove = pisnd_card_remove,
};

static int pisnd_init_gpio(struct device *dev)
{
	osr0 = gpiod_get_index(dev, "osr", 0, GPIOD_ASIS);
	osr1 = gpiod_get_index(dev, "osr", 1, GPIOD_ASIS);
	osr2 = gpiod_get_index(dev, "osr", 2, GPIOD_ASIS);

	reset = gpiod_get_index(dev, "reset", 0, GPIOD_ASIS);

	button = gpiod_get_index(dev, "button", 0, GPIOD_ASIS);

	gpiod_direction_output(osr0, 1);
	gpiod_direction_output(osr1, 1);
	gpiod_direction_output(osr2, 1);
	gpiod_direction_output(reset, 1);

	gpiod_set_value(reset, false);
	gpiod_set_value(osr0, true);
	gpiod_set_value(osr1, false);
	gpiod_set_value(osr2, false);
	gpiod_set_value(reset, true);

	gpiod_export(button, false);

	return 0;
}

static int pisnd_uninit_gpio(void)
{
	int i;

	struct gpio_desc **gpios[] = {
	    &osr0, &osr1, &osr2, &reset, &button,
	};

	gpiod_unexport(button);

	for (i = 0; i < ARRAY_SIZE(gpios); ++i) {
		if (*gpios[i] == NULL) {
			printd("weird, GPIO[%d] is NULL already\n", i);
			continue;
		}

		gpiod_put(*gpios[i]);
		*gpios[i] = NULL;
	}

	return 0;
}

static struct kobject *pisnd_kobj;

static ssize_t pisnd_serial_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", "serial");
}

static ssize_t pisnd_id_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buf)
{
	return sprintf(buf, "%s\n", "ïd");
}

static ssize_t pisnd_version_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", "version");
}

static ssize_t pisnd_led_store(struct kobject *kobj,
			       struct kobj_attribute *attr, const char *buf,
			       size_t length)
{
	uint32_t timeout;
	int err;
	/*
		err = kstrtou32(buf, 10, &timeout);

		if (err == 0 && timeout <= 255)
			pisnd_spi_flash_leds(timeout);
	i*/
	return length;
}

static struct kobj_attribute pisnd_serial_attribute =
    __ATTR(serial, 0444, pisnd_serial_show, NULL);
static struct kobj_attribute pisnd_id_attribute =
    __ATTR(id, 0444, pisnd_id_show, NULL);
static struct kobj_attribute pisnd_version_attribute =
    __ATTR(version, 0444, pisnd_version_show, NULL);
static struct kobj_attribute pisnd_led_attribute =
    __ATTR(led, 0644, NULL, pisnd_led_store);

static struct attribute *attrs[] = {
    &pisnd_serial_attribute.attr, &pisnd_id_attribute.attr,
    &pisnd_version_attribute.attr, &pisnd_led_attribute.attr, NULL};

static struct attribute_group attr_group = {.attrs = attrs};

static int pisnd_probe(struct platform_device *pdev)
{
	int ret = 0;
	int i;

	//	ret = pisnd_spi_init(&pdev->dev);
	if (ret < 0) {
		printe("pisnd_spi_init failed: %d\n", ret);
		return ret;
	}
	/*
		printi("Detected pisound card:\n");
		printi("\tSerial:  %s\n", pisnd_spi_get_serial());
		printi("\tVersion: %s\n", pisnd_spi_get_version());
		printi("\tId:      %s\n", pisnd_spi_get_id());
	*/
	pisnd_kobj = kobject_create_and_add("pisound", kernel_kobj);
	if (!pisnd_kobj) {
		// pisnd_spi_uninit();
		return -ENOMEM;
	}

	ret = sysfs_create_group(pisnd_kobj, &attr_group);
	if (ret < 0) {
		// pisnd_spi_uninit();
		kobject_put(pisnd_kobj);
		return -ENOMEM;
	}

	pisnd_init_gpio(&pdev->dev);
	pisnd_card.dev = &pdev->dev;

	if (pdev->dev.of_node) {
		struct device_node *i2s_node;

		i2s_node =
		    of_parse_phandle(pdev->dev.of_node, "i2s-controller", 0);

		for (i = 0; i < pisnd_card.num_links; ++i) {
			struct snd_soc_dai_link *dai = &pisnd_dai[i];

			if (i2s_node) {
				dai->cpu_dai_name = NULL;
				dai->cpu_of_node = i2s_node;
				dai->platform_name = NULL;
				dai->platform_of_node = i2s_node;
				// dai->stream_name = pisnd_spi_get_serial();
			}
		}
	}

	ret = snd_soc_register_card(&pisnd_card);

	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			printe("snd_soc_register_card() failed: %d\n", ret);
		pisnd_uninit_gpio();
		kobject_put(pisnd_kobj);
		// pisnd_spi_uninit();
	}

	return ret;
}

static int pisnd_remove(struct platform_device *pdev)
{
	printi("Unloading.\n");

	if (pisnd_kobj) {
		kobject_put(pisnd_kobj);
		pisnd_kobj = NULL;
	}

	// pisnd_spi_uninit();

	/* Turn off */
	gpiod_set_value(reset, false);
	pisnd_uninit_gpio();

	return snd_soc_unregister_card(&pisnd_card);
}

MODULE_DEVICE_TABLE(of, pisound_of_match);

static struct platform_driver pisnd_driver = {
    .driver =
	{
	    .name = "snd-matrixio-audio",
	    .owner = THIS_MODULE,
	    .of_match_table = pisound_of_match,
	},
    .probe = pisnd_probe,
    .remove = pisnd_remove,
};

module_platform_driver(pisnd_driver);

MODULE_AUTHOR("Giedrius Trainavicius <giedrius@blokas.io>");
MODULE_DESCRIPTION("ASoC Driver for pisound, http://blokas.io/pisound");
MODULE_LICENSE("GPL v2");