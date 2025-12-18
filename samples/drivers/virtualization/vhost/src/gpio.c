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

#define GPIO0_NODE DT_NODELABEL(gpio0)

LOG_MODULE_REGISTER(vhost_gpio);

static struct vhost_iovec riovec[16];
static struct vhost_iovec wiovec[16];

static struct vringh_iov riov = {
	.iov = riovec,
	.max_num = 16,
};

static struct vringh_iov wiov = {
	.iov = wiovec,
	.max_num = 16,
};

struct virtio_gpio_request {
	uint16_t type;
	uint16_t gpio;
	uint32_t value;
};

struct virtio_gpio_response {
	uint8_t status;
	uint8_t value;
};

static struct vringh vrh_inst;

static void vringh_kick_handler(struct vringh *vrh)
{
	LOG_DBG("%s: queue_id=%lu", __func__, vrh->queue_id);
	uint16_t head;

	while (true) {
		int ret = vringh_getdesc(vrh, &riov, &wiov, &head);

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
			const struct device *const dev = DEVICE_DT_GET(GPIO0_NODE);

			// printk("    riov.iov[0].iov_base: %p\n", riov.iov[s].iov_base);
			// printk("    riov.iov[0].iov_len: %u\n", riov.iov[s].iov_len);
			// Read request
			LOG_HEXDUMP_INF(riov.iov[0].iov_base, riov.iov[s].iov_len, "riov.iov[0]");
			mem_addr_t addr_base = (mem_addr_t)riov.iov[s].iov_base;
			req.type = sys_read16(addr_base + 0);
			req.gpio = sys_read16(addr_base + 2);
			req.value = sys_read32(addr_base + 4);

			// Case-by-case handling based on request type
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
					break;
				}
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
			} break;
			case VIRTIO_GPIO_MSG_SET_DIRECTION: {
				gpio_flags_t flags = 0;
				gpio_flags_t mask;

				if (req.value > VIRTIO_GPIO_DIRECTION_IN) {
					printk("Unexpeded value (req.value): %d\n", req.value);
					resp.status = VIRTIO_GPIO_STATUS_ERR;
					resp.value = 0;
					break;
				}

				ret = gpio_pin_get_config(dev, req.gpio, &flags);
				if (ret < 0) {
					printk("failed to set direction\n");
					resp.status = VIRTIO_GPIO_STATUS_ERR;
					resp.value = 0;
					break;
				}
				printk("Read pin status: %x\n", flags);

				// Lower bits of GPIO_OUTPUT and GPIO_INPUT
				mask = GPIO_OUTPUT | GPIO_INPUT;
				flags &= ~(flags & mask);

				// Upper bits of GPIO_OUTPUT or GPIO_INPUT
				switch (req.value) {
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
					break;
				}

				resp.status = VIRTIO_GPIO_STATUS_OK;
				resp.value = 0;
				printk("Write pin status: %x\n", flags);
			} break;
			case VIRTIO_GPIO_MSG_GET_VALUE: {
				ret = gpio_pin_get(dev, req.gpio);
				if (ret < 0) {
					printk("failed to get value\n");
					resp.status = VIRTIO_GPIO_STATUS_ERR;
					resp.value = 0;
					break;
				}

				printk("succeeded to get value\n");
				resp.status = VIRTIO_GPIO_STATUS_OK;
				resp.value = ret;
			} break;
			case VIRTIO_GPIO_MSG_SET_VALUE: {
				ret = gpio_pin_set(dev, req.gpio, req.value);
				if (ret < 0) {
					printk("failed to set value\n");
					resp.status = VIRTIO_GPIO_STATUS_ERR;
					resp.value = 0;
					break;
				}

				printk("succeeded to set value\n");
				resp.status = VIRTIO_GPIO_STATUS_OK;
				resp.value = 0;
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

void gpio_queue_ready(const struct device *dev, uint16_t qid, void *data)
{
	LOG_DBG("%s(dev=%p, qid=%u, data=%p)", __func__, dev, qid, data);

	/* Initialize iovecs before descriptor processing */
	vringh_iov_init(&riov, riov.iov, riov.max_num);
	vringh_iov_init(&wiov, wiov.iov, wiov.max_num);

	int err = vringh_init_device(&vrh_inst, dev, qid, vringh_kick_handler);

	if (err) {
		LOG_ERR("vringh_init_device failed: %d", err);
		return;
	}
}
