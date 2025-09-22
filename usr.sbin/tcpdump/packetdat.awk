BEGIN	{
	# we need to know (usual) packet size to convert byte numbers
	# to packet numbers
	if (packetsize <= 0)
		packetsize = 512
	}
$5 !~ /[SR]/	{
	# print out per-packet data in the form:
	#  <packet #>
	#  <start sequence #>
	#  <1st send time>
	#  <last send time>
	#  <1st ack time>
	#  <last ack time>
	#  <# sends>
	#  <# acks>

	n = split ($1,t,":")
	tim = t[1]*3600 + t[2]*60 + t[3]
	if ($6 != "ack") {
		i = index($6,":")
		strtSeq = substr($6,1,i-1)
		id = 1.5 + (strtSeq - 1) / packetsize
		id -= id % 1
		if (maxId < id)
			maxId = id
		if (firstSend[id] == 0) {
			firstSend[id] = tim
			seqNo[id] = strtSeq
		}
		lastSend[id] = tim
		timesSent[id]++
		totalPackets++
	} else {
		id = 1 + ($7 - 2) / packetsize
		id -= id % 1
		timesAcked[id]++
		if (firstAck[id] == 0)
			firstAck[id] = tim
		lastAck[id] = tim
		totalAcks++
	}
	}
END	{
	print "# " maxId " chunks.  " totalPackets " packets sent.  " \
		totalAcks " acks."
	# for packets that were implicitly acked, make the ack time
	# be the ack time of next explicitly acked packet.
	for (i = maxId-1; i > 0; --i)
		while (i > 0 && firstAck[i] == 0) {
			lastAck[i] = firstAck[i] = firstAck[i+1]
			--i
		}
	tzero = firstSend[1]
	for (i = 1; i <= maxId; i++)
		printf "%d\t%d\t%.2f\t%.2f\t%.2f\t%.2f\t%d\t%d\n",\
			i, seqNo[i], \
			firstSend[i] - tzero, lastSend[i] - tzero,\
			firstAck[i] - tzero, lastAck[i] - tzero,\
			timesSent[i], timesAcked[i]
	}
