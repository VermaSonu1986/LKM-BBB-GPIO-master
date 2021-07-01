/**
 * @file   gpio_test.c
 * @author Sonu Verma
 * @date   21 April 2021
 * @brief  A kernel module for controlling a GPIO LED/button pair for Beaglebone blue
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>                 // Required for the GPIO functions
#include <linux/interrupt.h>            // Required for the IRQ code
#include <linux/kthread.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SONU VERMA");
MODULE_DESCRIPTION("A BTN/LED test driver for the BBB");
MODULE_VERSION("0.1");

static unsigned int gpioLedRED = 		66;       	///< hard coding the LED gpio for this example to P9_23 (GPIO49)
static unsigned int gpioLedGREEN = 		67;		// gpio assgined to the GREEN LED 
static unsigned int gpioButton = 		69;   		///< hard coding the button gpio for this example to P9_27 (GPIO115)
static unsigned int irqNumber;          			///< Used to share the IRQ number within this file
static unsigned int numberPresses = 		0;  		///< For information, store the number of button presses
static bool	    currentStateLedRED = 	1;     		///< Is the LED on or off? Used to invert its state (off by default)
static bool         currentStateLedGREEN = 	0;
static bool 	    ledOn = 			0;
enum modes           				{OFF, ON, FLASH}; 
static struct task_struct *task;
static enum modes mode = 			FLASH;	        ///< Default mode is flashing
static unsigned int blinkPeriod = 		1000;		///< The blink period in ms
/// Function prototype for the custom IRQ handler function -- see below for the implementation
static irq_handler_t  ebbgpio_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs);


/** @brief The LED Flasher main kthread loop
 *
 *  @param arg A void pointer used in order to pass data to the thread
 *  @return returns 0 if successful
 */
static int kThread_run(void *arg){
   printk(KERN_INFO "EBB LED: Thread has started running \n");
   while(!kthread_should_stop()){           		// Returns true when kthread_stop() is called
      set_current_state(TASK_RUNNING);
      if (mode==FLASH) ledOn = !ledOn;      		// Invert the LED state
      else if (mode==ON) ledOn = true;
      else ledOn = false;
      gpio_set_value(gpioLedGREEN,ledOn);
      gpio_set_value(gpioLedRED, ledOn);       		// Use the LED state to light/turn off the LED
      set_current_state(TASK_INTERRUPTIBLE);
      msleep(blinkPeriod/3);                		// millisecond sleep for half of the period
      }
return 0;
}


/** @brief The LKM initialization function
 *  The static keyword restricts the visibility of the function to within this C file. The __init
 *  macro means that for a built-in driver (not a LKM) the function is only used at initialization
 *  time and that it can be discarded and its memory freed up after that point. In this example this
 *  function sets up the GPIOs and the IRQ
 *  @return returns 0 if successful
 */


static int __init ebbgpio_init(void){
   int result = 0;
   printk(KERN_INFO "GPIO_TEST: Initializing the GPIO_TEST LKM\n");
   // Is the GPIO a valid GPIO number (e.g., the BBB has 4x32 but not all available)
   if (!gpio_is_valid(gpioLedRED)&&(!gpio_is_valid(gpioLedGREEN))){
      printk(KERN_INFO "GPIO_TEST: invalid LED:RED/GREEN GPIO\n");
      return -ENODEV;
   }
   // Going to set up the LED. It is a GPIO in output mode and will be on by default

   gpio_request(gpioLedRED, "sysfs");          	// gpioLED is hardcoded to 49, request it
   gpio_request(gpioLedGREEN, "sysfs");        	// request for GREEN LED
   gpio_direction_output(gpioLedRED, currentStateLedRED);   // Set the gpio to be in output mode and on
   gpio_direction_output(gpioLedGREEN, currentStateLedGREEN);
// gpio_set_value(gpioLED, ledOn);          	// Not required as set by line above (here for reference)
   gpio_export(gpioLedRED, false);             	// Causes gpio49 to appear in /sys/class/gpio
			                    	// the bool argument prevents the direction from being changed
   gpio_export(gpioLedGREEN,false);
   gpio_request(gpioButton, "sysfs");       // Set up the gpioButton
   gpio_direction_input(gpioButton);        // Set the button GPIO to be an input
   gpio_set_debounce(gpioButton, 200);      // Debounce the button with a delay of 200ms
   gpio_export(gpioButton, false);          // Causes gpio115 to appear in /sys/class/gpio
			                    // the bool argument prevents the direction from being changed
   // Perform a quick test to see that the button is working as expected on LKM load
   printk(KERN_INFO "GPIO_TEST: The button state is currently: %d\n", gpio_get_value(gpioButton));

   // GPIO numbers and IRQ numbers are not the same! This function performs the mapping for us
   irqNumber = gpio_to_irq(gpioButton);
   printk(KERN_INFO "GPIO_TEST: The button is mapped to IRQ: %d\n", irqNumber);

   // This next call requests an interrupt line
   result = request_irq(irqNumber,             // The interrupt number requested
                        (irq_handler_t) ebbgpio_irq_handler, // The pointer to the handler function below
                        IRQF_TRIGGER_RISING,   // Interrupt on rising edge (button press, not release)
                        "ebb_gpio_handler",    // Used in /proc/interrupts to identify the owner
                        NULL);                 // The *dev_id for shared interrupt lines, NULL is okay

   printk(KERN_INFO "GPIO_TEST: The interrupt request result is: %d\n", result);

   task = kthread_run(kThread_run, NULL, "LED_thread");  // Start the LED flashing thread
   if(IS_ERR(task)){                                     // Kthread name is LED_flash_thread
      printk(KERN_ALERT "EBB LED: failed to create the task\n");
      return PTR_ERR(task);
      }
 return result;
}

/** @brief The LKM cleanup function
 *  Similar to the initialization function, it is static. The __exit macro notifies that if this
 *  code is used for a built-in driver (not a LKM) that this function is not required. Used to release the
 *  GPIOs and display cleanup messages.
 */
static void __exit ebbgpio_exit(void){
   kthread_stop(task);
   printk(KERN_INFO "GPIO_TEST: The button state is currently: %d\n", gpio_get_value(gpioButton));
   printk(KERN_INFO "GPIO_TEST: The button was pressed %d times\n", numberPresses);
   gpio_set_value(gpioLedRED, 0);              // Turn the LED off, makes it clear the device was unloaded
   gpio_set_value(gpioLedGREEN,0);
   gpio_unexport(gpioLedRED);                  // Unexport the LED GPIO
   gpio_unexport(gpioLedGREEN);
   free_irq(irqNumber, NULL);               // Free the IRQ number, no *dev_id required in this case
   gpio_unexport(gpioButton);               // Unexport the Button GPIO
   gpio_free(gpioLedRED);                      // Free the LED GPIO
   gpio_free(gpioLedGREEN);
   gpio_free(gpioButton);                   // Free the Button GPIO
   printk(KERN_INFO "GPIO_TEST: Goodbye from the LKM!\n");
}

/** @brief The GPIO IRQ Handler function
 *  This function is a custom interrupt handler that is attached to the GPIO above. The same interrupt
 *  handler cannot be invoked concurrently as the interrupt line is masked out until the function is complete.
 *  This function is static as it should not be invoked directly from outside of this file.
 *  @param irq    the IRQ number that is associated with the GPIO -- useful for logging.
 *  @param dev_id the *dev_id that is provided -- can be used to identify which device caused the interrupt
 *  Not used in this example as NULL is passed.
 *  @param regs   h/w specific register values -- only really ever used for debugging.
 *  return returns IRQ_HANDLED if successful -- should return IRQ_NONE otherwise.
 */
static irq_handler_t ebbgpio_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs){
   //gpio_set_value(gpioLedRED,(!gpio_get_value(gpioLedRED)));
   //gpio_set_value(gpioLedGREEN,(!gpio_get_value(gpioLedGREEN)));                 // Invert the LED state on each button press
   //printk(KERN_INFO "GPIO_TEST: Interrupt! (button state is %d)\n", gpio_get_value(gpioButton));
   printk(KERN_INFO "Button pressed count is %d\n", numberPresses);
   if(mode == FLASH)
    {
      mode = ON;
    }
   else
    {
      mode = FLASH;
    }
   numberPresses++;                         // Global counter, will be outputted when the module is unloaded
   return (irq_handler_t) IRQ_HANDLED;      // Announce that the IRQ has been handled correctly
}


/// This next calls are  mandatory -- they identify the initialization function
/// and the cleanup function (as above).
module_init(ebbgpio_init);
module_exit(ebbgpio_exit);
