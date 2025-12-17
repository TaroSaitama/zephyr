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

static void vringh_kick_handler(struct vringh *vrh)
{
	LOG_DBG("%s: queue_id=%lu", __func__, vrh->queue_id);
	uint16_t head;

	while (true) {
		int ret = vringh_getdesc(vrh, &riov, &wiov, &head);
		//printk("Get a descriptor, riov.used: %d, riov.used: %d\n", riov.used, wiov.used);

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
			struct virtio_gpio_request req;
			struct virtio_gpio_response resp = {0};
			struct device *dev = DEVICE_DT_GET(GPIO_EMUL_0_NODE);
			//printk("    riov.iov[0].iov_base: %p\n", riov.iov[s].iov_base);
			//printk("    riov.iov[0].iov_len: %u\n", riov.iov[s].iov_len);
            // Read request
			LOG_HEXDUMP_INF(riov.iov[0].iov_base, riov.iov[s].iov_len, "riov.iov[0]");
			mem_addr_t addr_base = (mem_addr_t)riov.iov[s].iov_base;
			req.type = sys_read16(addr_base + 0);
			req.gpio = sys_read16(addr_base + 2);
			req.value = sys_read32(addr_base + 4);

            // Case-by-case handling based on request type
			ret = 0;
			switch (req.type) {
			case VIRTIO_GPIO_MSG_GET_LINE_NAMES: {
				printk("VIRTIO_GPIO_MSG_GET_LINENAME is not implemented\n");
			} break;
			case VIRTIO_GPIO_MSG_GET_DIRECTION: {
                gpio_flags_t flags = 0;
                ret = gpio_pin_get_config(dev, req.gpio, &flags);
 				if (ret < 0) {
					printk("failed to get direction\n");
					resp.status = VIRTIO_GPIO_STATUS_ERR;
                    resp.value = 0;
				} else {
                    flags &= (GPIO_OUTPUT | GPIO_INPUT);
                    printk("Get direction: %d\n", flags);
                    switch (flags) { // none, out, in
                        case GPIO_DISCONNECTED: {
                            resp.status = VIRTIO_GPIO_STATUS_OK;
                            resp.value = VIRTIO_GPIO_DIRECTION_NONE;
                        } break;
                       case GPIO_OUTPUT: {
                            resp.status = VIRTIO_GPIO_STATUS_OK;
                            resp.value = VIRTIO_GPIO_DIRECTION_OUT;
                        } break;
                        case GPIO_INPUT: {
                            resp.status = VIRTIO_GPIO_STATUS_OK;
                            resp.value = VIRTIO_GPIO_DIRECTION_IN;
                        } break;
                        default: {
                            resp.status = VIRTIO_GPIO_STATUS_ERR;
                            resp.value = 0;
                        } break;
                    }
                }

				// Get direction
				// Assume the states of input and output are mutually exclusive.
                /*
				int ret1 = gpio_pin_is_input(dev, req.gpio);
				ret = gpio_pin_is_output(dev, req.gpio);
                printk("gpio_pin_is_output: %d\n", ret);
				if (ret < 0 || ret1 < 0) {
					printk("failed to get direction\n");
					resp.status = VIRTIO_GPIO_STATUS_ERR;
                    resp.value = 0;
				} else {
					//printk("succeeded to get direction\n");
					if (ret != 0) {
                        printk("return direction is out\n");
                        resp.status = VIRTIO_GPIO_STATUS_OK;
						resp.value = VIRTIO_GPIO_DIRECTION_OUT;
					} else {
                        printk("return direction is in\n");
                        resp.status = VIRTIO_GPIO_STATUS_OK;
						resp.value = VIRTIO_GPIO_DIRECTION_IN;
					}
				}
                */
			} break;
			case VIRTIO_GPIO_MSG_SET_DIRECTION: {
				// function to set direction
                gpio_flags_t flags = 0;
                ret = gpio_pin_get_config(dev, req.gpio, &flags);
				if (ret < 0) {
					printk("failed to set direction\n");
					resp.status = VIRTIO_GPIO_STATUS_ERR;
                    resp.value = 0;
				} else {
                    printk("Read pin status: %x\n", flags);
                    // Lower bits of GPIO_OUTPUT and GPIO_INPUT 
                    gpio_flags_t mask = GPIO_OUTPUT | GPIO_INPUT;
                    flags &= ~(flags & mask);
                    // Upper bits of GPIO_OUTPUT or GPIO_INPUT
                    switch (req.value) { // none, out, in
                        case VIRTIO_GPIO_DIRECTION_NONE: {
                            printk("Set direction none (req.value): %d\n", req.value); 
                            flags |= GPIO_DISCONNECTED;
                        } break;
                        case VIRTIO_GPIO_DIRECTION_OUT: {
                            printk("Set direction out (req.value): %d\n", req.value); 
                            flags |= GPIO_OUTPUT;
                        } break;
                        case VIRTIO_GPIO_DIRECTION_IN: {
                            printk("Set direction in (req.value): %d\n", req.value);
                            flags |= GPIO_INPUT;
                        } break;
                    }
                    ret = gpio_pin_configure(dev, req.gpio, flags);
                    if (ret < 0) {
                        resp.status = VIRTIO_GPIO_STATUS_ERR;
                        resp.value = 0;
                    } else {
                        resp.status = VIRTIO_GPIO_STATUS_OK;
                        resp.value = 0;
                        printk("Write pin status: %x\n", flags);
                    }
                }
			} break;
			case VIRTIO_GPIO_MSG_GET_VALUE: {
				ret = gpio_pin_get(dev, req.gpio);
				if (ret < 0) {
					printk("failed to get value\n");
					resp.status = VIRTIO_GPIO_STATUS_ERR;
                    resp.value = 0;
				} else {
					printk("succeeded to get value\n");
					resp.status = VIRTIO_GPIO_STATUS_OK;
					resp.value = ret; // 0 or 1 depending on value of GPIO
				}
			} break;
			case VIRTIO_GPIO_MSG_SET_VALUE: {
				ret = gpio_pin_set(dev, req.gpio, req.value);
				if (ret < 0) {
					printk("failed to set value\n");
					resp.status = VIRTIO_GPIO_STATUS_ERR;
                    resp.value = 0;
				} else {
					printk("succeeded to set value\n");
					resp.status = VIRTIO_GPIO_STATUS_OK;
                    resp.value = 0;
				}
			} break;
			case VIRTIO_GPIO_MSG_SET_IRQ_TYPE: {
				printk("VIRTIO_GPIO_MSG_SET_IRQ_TYPE is not implemented\n");
			} break;
			}
			// wirite response to memory
			addr_base = (mem_addr_t)wiov.iov[s].iov_base;
			sys_write8(resp.status, addr_base + 0);
			sys_write8(resp.value, addr_base + 1);
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
