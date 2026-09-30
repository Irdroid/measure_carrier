# 1. Unbind the interface from its current driver
echo "1-4:1.0" | sudo tee /sys/bus/usb/drivers/CURRENT_DRIVER/unbind

# 2. Tell cdc_acm to recognize the IDs (if not done already)
echo "04d8 fd08" | sudo tee /sys/bus/usb/drivers/cdc_acm/new_id

# 3. Force cdc_acm to probe the newly freed interface
echo "1-4:1.0" | sudo tee /sys/bus/usb/drivers/cdc_acm/bind
