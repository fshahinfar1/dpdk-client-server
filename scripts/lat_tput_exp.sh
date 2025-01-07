#! /bin/bash

# Measure the latency of the system when varying the offered load
# Experiment design:
# Setup the device under test.
# Run a background traffic at a certain load (offered load)
# On another system measure the latency with low load

# NOTE: read through the file and update the configs. Also the CPU/numa node
# selections and other configs should be tweaked.

# Expet to find this binary on the load generator machines at $app.
app=/users/$USER/gen/dpdk-client-server/build/app
# Update this based on your systems.
dpdk_pci="03:00.1"
dut=192.168.1.1
bg_load_gen=192.168.1.2
fg_load_gen=192.168.1.3

bg_load_gen_ctrl=128.110.218.116
fg_load_gen_ctrl=128.110.218.118

# Configure the load steps depending to your experiment (packet per second)
load_steps=( 10000 100000 1000000 2000000 )
# load_steps=( 10000 )
bg_time=50
fg_time=10
lat_tmp_file=/tmp/measurements.txt
store_dir=$HOME/results/

run_bg() {
	# @param $1: offered load
	load=$1
	cmd="sudo $app --lcores '0@(2,4)' -a $dpdk_pci --
	--client --ip-local $bg_load_gen
	--ip-dest $dut --duration $bg_time --batch 32 --rate $load &> /dev/null &"
	ssh $USER@$bg_load_gen_ctrl $cmd
}

run_fg() {
	cmd="sudo $app -l 2 -a $dpdk_pci --
	--latency-client --ip-local $fg_load_gen
	--ip-dest $dut --hdr-encap-sz 20
	--delay 1000000 &> $lat_tmp_file &"
	ssh $USER@$fg_load_gen_ctrl $cmd
}

gather_lat_measurments() {
	# @param $1: offered load
	load=$1
	scp $USER@$fg_load_gen_ctrl:$lat_tmp_file $store_dir/load_at_$load.txt
}

clean_everything() {
	ssh $USER@$bg_load_gen_ctrl "sudo pkill -SIGINT app"
	ssh $USER@$fg_load_gen_ctrl "sudo pkill -SIGINT app"
}

mkdir -p $store_dir
for offered_load in ${load_steps[@]}; do
	echo "at Load = $offered_load"
	run_bg $offered_load &
	sleep 5
	run_fg &
	# NOTE: right now the latency app does not terminate base on time and wait
	# for a signal.So this script does the timing...
	sleep $fg_time
	sleep 1
	clean_everything
	sleep 5
	gather_lat_measurments $offered_load
	sleep 5
done
