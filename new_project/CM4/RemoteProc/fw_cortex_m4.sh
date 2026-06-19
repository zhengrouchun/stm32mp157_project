#!/bin/sh

rproc_class_dir="/sys/class/remoteproc/remoteproc0"
fmw_dir="/lib/firmware"
<<<<<<< HEAD
fmw_name="RPMsg_UART_ADC_CM4.elf"
=======
fmw_name="RPMsg_TEST_CM4.elf"
>>>>>>> 042c0689b3d30a66b27a2a4342a332c9bfc9e0a9

cd $(/usr/bin/dirname $(/usr/bin/readlink -f $0))

if [ $1 == "start" ]
then
        # Start the firmware
        /bin/rm -f /lib/firmware/$fmw_name
        /bin/ln -s $PWD/lib/firmware/$fmw_name $fmw_dir
        /bin/echo -n "$fmw_name" > $rproc_class_dir/firmware
        /bin/echo -n start > $rproc_class_dir/state
fi

if [ $1 == "stop" ]
then
        # Stop the firmware
        /bin/echo -n stop > $rproc_class_dir/state
fi
