#ifndef __DRIVER_GPIO_H__
#define __DRIVER_GPIO_H__

/** @brief Host-test GPIO number representation. */
typedef int gpio_num_t;

#define GPIO_NUM_NC (-1)
#define GPIO_NUM_0  0
#define GPIO_NUM_4  4
#define GPIO_NUM_5  5
#define GPIO_NUM_6  6
#define GPIO_NUM_7  7
#define GPIO_NUM_11 11
#define GPIO_NUM_12 12
#define GPIO_NUM_13 13
#define GPIO_NUM_21 21

#define GPIO_INTR_POSEDGE 1

int gpio_intr_enable(gpio_num_t gpio_num);
int gpio_intr_disable(gpio_num_t gpio_num);

#endif /* __DRIVER_GPIO_H__ */
