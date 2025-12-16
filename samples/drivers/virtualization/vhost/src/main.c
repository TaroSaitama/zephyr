/*
 * Copyright (c) 2025 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/vhost.h>
#include <zephyr/drivers/vhost/vringh.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

/* Possible values of the status field */
#define VIRTIO_GPIO_STATUS_OK  0x0
#define VIRTIO_GPIO_STATUS_ERR 0x1

/* GPIO message types */
#define VIRTIO_GPIO_MSG_GET_LINE_NAMES 0x0001
#define VIRTIO_GPIO_MSG_GET_DIRECTION  0x0002
#define VIRTIO_GPIO_MSG_SET_DIRECTION  0x0003
#define VIRTIO_GPIO_MSG_GET_VALUE      0x0004
#define VIRTIO_GPIO_MSG_SET_VALUE      0x0005
#define VIRTIO_GPIO_MSG_SET_IRQ_TYPE   0x0006

/* GPIO Direction types */
#define VIRTIO_GPIO_DIRECTION_NONE 0x00
#define VIRTIO_GPIO_DIRECTION_OUT  0x01
#define VIRTIO_GPIO_DIRECTION_IN   0x02

/* GPIO interrupt types */
#define VIRTIO_GPIO_IRQ_TYPE_NONE         0x00
#define VIRTIO_GPIO_IRQ_TYPE_EDGE_RISING  0x01
#define VIRTIO_GPIO_IRQ_TYPE_EDGE_FALLING 0x02
#define VIRTIO_GPIO_IRQ_TYPE_EDGE_BOTH    0x03
#define VIRTIO_GPIO_IRQ_TYPE_LEVEL_HIGH   0x04
#define VIRTIO_GPIO_IRQ_TYPE_LEVEL_LOW    0x08

#define GPIO_EMUL_0_NODE DT_NODELABEL(gpio_emul_0)

LOG_MODULE_REGISTER(vhost);

#define QUEUE_READY_HANDLER_NAME(node_name) UTIL_CAT(node_name, _queue_ready)

#define QUEUE_READY_HANDLER_PROTODECL(sym)                                                         \
	void QUEUE_READY_HANDLER_NAME(sym)(const struct device *dev, uint16_t qid, void *data);

#define DECL_HANDLER(inst, x, y)                                                                   \
	QUEUE_READY_HANDLER_PROTODECL(DT_NODE_FULL_NAME_UNQUOTED(DT_INST(inst, xen_vhost_mmio)))

#define REGISTER_HANDLER(inst, x, y)                                                               \
	{                                                                                          \
		const struct device *dev = DEVICE_DT_GET(DT_INST(inst, xen_vhost_mmio));           \
		register_handler(dev, QUEUE_READY_HANDLER_NAME(DT_NODE_FULL_NAME_UNQUOTED(         \
					      DT_INST(inst, xen_vhost_mmio))));                    \

struct virtio_gpio_request {
	uint16_t type;
	uint16_t gpio;
	uint32_t value;
};

struct virtio_gpio_response {
	uint8_t status;
	uint8_t value;
};
struct vringh vrh_inst;

uint8_t line_status = 1;

static void vringh_kick_handler(struct vringh *vrh)
{
	LOG_DBG("%s: queue_id=%lu", __func__, vrh->queue_id);
	uint16_t head;

	while (true) {
		int ret = vringh_getdesc(vrh, &riov, &wiov, &head);
		printk("Get a descriptor, riov.used: %d, riov.used: %d\n", riov.used, wiov.used);

		if (ret < 0) {
			LOG_ERR("vringh_getdesc failed: %d", ret);
			return;
		}

		if (ret == 0) {
			printk("Exit 0\n");
			return;
		}
		printk("riov.used: %d\n", riov.used);
		for (uint32_t s = 0; s < riov.used; s++) {
			printk("    riov.iov[0].iov_base: %p\n", riov.iov[s].iov_base);
			printk("    riov.iov[0].iov_len: %u\n", riov.iov[s].iov_len);
			LOG_HEXDUMP_INF(riov.iov[0].iov_base, riov.iov[s].iov_len, "riov.iov[0]");
			struct virtio_gpio_request req;
			mem_addr_t addr_base = (mem_addr_t)riov.iov[s].iov_base;
			req.type = sys_read16(addr_base + 0);
			req.gpio = sys_read16(addr_base + 2);
			req.value = sys_read32(addr_base + 4);

			struct virtio_gpio_response res = {0};

			struct device *dev = DEVICE_DT_GET(GPIO_EMUL_0_NODE);
			ret = 0;
			switch (req.type) {
			case VIRTIO_GPIO_MSG_GET_LINE_NAMES: {
				printk("VIRTIO_GPIO_MSG_GET_LINENAME is not implemented\n");
			} break;
			case VIRTIO_GPIO_MSG_GET_DIRECTION: {
				// Get direction
				// Assume the states of input and output are mutually exclusive.
				printk("prev line of gpio_pin_is_out()\n");
				ret = gpio_pin_is_output(dev, req.gpio);
				int ret1 = gpio_pin_is_input(dev, req.gpio);
				printk("lataer line of gpio_pin_is_out()\n");
				if (ret < 0 || ret1 < 0) {
					printk("failed to get direction\n");
					res.status = VIRTIO_GPIO_STATUS_ERR;
				} else {
					printk("succeeded to get direction\n");
					res.status = VIRTIO_GPIO_STATUS_OK;
					if (ret == 1) {
						res.value = VIRTIO_GPIO_DIRECTION_OUT;
					} else {
						res.value = VIRTIO_GPIO_DIRECTION_IN;
					}
				}
			} break;
			case VIRTIO_GPIO_MSG_SET_DIRECTION: {
				uint8_t line = req.gpio;
				uint8_t direct = req.value;
				line_status = direct;
				// function to set direction
				ret = gpio_pin_set(dev, line, direct);
				res.value = 0;
				if (ret < 0) {
					printk("failed to set direction\n");
					res.status = VIRTIO_GPIO_STATUS_ERR;
				} else {
					printk("succeeded to set direction\n");
					res.status = VIRTIO_GPIO_STATUS_OK;
				}
			} break;
			case VIRTIO_GPIO_MSG_GET_VALUE: {
				uint8_t line = req.gpio;
				ret = gpio_pin_get(dev, line);
				if (ret < 0) {
					printk("failed to get value\n");
					res.status = VIRTIO_GPIO_STATUS_ERR;
				} else {
					printk("succeeded to get value\n");
					res.status = VIRTIO_GPIO_STATUS_OK;
					res.value = ret; // 0 or 1 depending on value of GPIO
				}
			} break;
			case VIRTIO_GPIO_MSG_SET_VALUE: {
				uint8_t line = req.gpio;
				uint8_t value = req.value;
				ret = gpio_pin_set(dev, line, value);
				res.value = 0;
				if (ret < 0) {
					printk("failed to set value\n");
					res.status = VIRTIO_GPIO_STATUS_ERR;
				} else {
					printk("succeeded to set value\n");
					res.status = VIRTIO_GPIO_STATUS_OK;
				}
			} break;
			case VIRTIO_GPIO_MSG_SET_IRQ_TYPE: {
				printk("VIRTIO_GPIO_MSG_SET_IRQ_TYPE is not implemented\n");
			} break;
			}
			// wirite response to memory
			addr_base = (mem_addr_t)wiov.iov[s].iov_base;
			sys_write8(res.status, addr_base + 0);
			sys_write8(res.value, addr_base + 1);
		}
		// osahou
		barrier_dmem_fence_full();

		uint32_t total_len = 0;
		for (uint32_t i = 0; i < wiov.used; i++) {
			total_len += wiov.iov[i].iov_len;
		}

		vringh_complete(vrh, head, total_len);
		printk("Notify of completion\n");

		if (vringh_need_notify(vrh) > 0) {
			vringh_notify(vrh);
		}

		/* Reset iovecs for next iteration */
		vringh_iov_reset(&riov);
		vringh_iov_reset(&wiov);
    }
}

DT_COMPAT_FOREACH_STATUS_OKAY_VARGS(xen_vhost_mmio, DECL_HANDLER, ())

int register_handler(const struct device *dev,
		     void (*handler)(const struct device *, uint16_t, void *))
{
	if (!device_is_ready(dev)) {
		LOG_ERR("VHost device %s not ready", dev->name);
		return -ENODEV;
	}

	LOG_INF("VHost device %s ready", dev->name);
	vhost_register_virtq_ready_cb(dev, handler, (void *)dev);

	return 0;
}

int main(void)
{
	DT_COMPAT_FOREACH_STATUS_OKAY_VARGS(xen_vhost_mmio, REGISTER_HANDLER, ())

	LOG_INF("VHost sample application started, waiting for guest connections...");
	k_sleep(K_FOREVER);

	return 0;
}
