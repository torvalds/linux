
is_consistent()
{
	val=

	active_low_sysfs=`cat $GPIO_SYSFS/gpio$nr/active_low`
	val_sysfs=`cat $GPIO_SYSFS/gpio$nr/value`
	dir_sysfs=`cat $GPIO_SYSFS/gpio$nr/direction`

	gpio_this_debugfs=`cat $GPIO_DEBUGFS |grep "gpio-$nr" | sed "s/(.*)//g"`
	dir_debugfs=`echo $gpio_this_debugfs | awk '{print $2}'`
	val_debugfs=`echo $gpio_this_debugfs | awk '{print $3}'`
	if [ $val_debugfs = "lo" ]; then
		val=0
	elif [ $val_debugfs = "hi" ]; then
		val=1
	fi

	if [ $active_low_sysfs = "1" ]; then
		if [ $val = "0" ]; then
			val="1"
		else
			val="0"
		fi
	fi

	if [ $val_sysfs = $val ] && [ $dir_sysfs = $dir_debugfs ]; then
		echo -n "."
	else
		echo "test fail, exit"
		die
	fi
}

test_pin_logic()
{
	nr=$1
	direction=$2
	active_low=$3
	value=$4

	echo $direction > $GPIO_SYSFS/gpio$nr/direction
	echo $active_low > $GPIO_SYSFS/gpio$nr/active_low
	if [ $direction = "out" ]; then
		echo $value > $GPIO_SYSFS/gpio$nr/value
	fi
	is_consistent $nr
}

test_one_pin()
{
	nr=$1

	echo -n "test pin<$nr>"

	echo $nr > $GPIO_SYSFS/export 2>/dev/null

	if [ X$? != X0 ]; then
		echo "test GPIO pin $nr failed"
		die
	fi

	#"Checking if the sysfs is consistent with debugfs: "
	is_consistent $nr

	#"Checking the logic of active_low: "
	test_pin_logic $nr out 1 1
	test_pin_logic $nr out 1 0
	test_pin_logic $nr out 0 1
	test_pin_logic $nr out 0 0

	#"Checking the logic of direction: "
	test_pin_logic $nr in 1 1
	test_pin_logic $nr out 1 0
	test_pin_logic $nr low 0 1
	test_pin_logic $nr high 0 0

	echo $nr > $GPIO_SYSFS/unexport

	echo "successful"
}

test_one_pin_fail()
{
	nr=$1

	echo $nr > $GPIO_SYSFS/export 2>/dev/null

	if [ X$? != X0 ]; then
		echo "test invalid pin $nr successful"
	else
		echo "test invalid pin $nr failed"
		echo $nr > $GPIO_SYSFS/unexport 2>/dev/null
		die
	fi
}

list_chip()
{
	echo `ls -d $GPIO_DRV_SYSFS/gpiochip* 2>/dev/null`
}

test_chip()
{
	chip=$1
	name=`basename $chip`
	base=`cat $chip/base`
	ngpio=`cat $chip/ngpio`
	printf "%-10s %-5s %-5s\n" $name $base $ngpio
	if [ $ngpio = "0" ]; then
		echo "number of gpio is zero is not allowed".
	fi
	test_one_pin $base
	test_one_pin $(($base + $ngpio - 1))
	test_one_pin $((( RANDOM % $ngpio )  + $base ))
}

test_chips_sysfs()
{
       gpiochip=`list_chip $module`
       if [ X"$gpiochip" = X ]; then
               if [ X"$valid" = Xfalse ]; then
                       echo "successful"
               else
                       echo "fail"
                       die
               fi
       else
               for chip in $gpiochip; do
                       test_chip $chip
               done
       fi
}

