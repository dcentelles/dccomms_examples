rosservice call /dccomms_netsim/add_custom_net_device "{dccommsId: 'node1', mac: 2, frameId: 'node1dev', maxBitRate: 2000, bitrate: 2000.27, bitrateSd: 0.0,
  maxDistance: 9000000, minDistance: 0, minPktErrorRate: 0.4, pktErrorRateIncPerMeter: 0.1}"
rosservice call /dccomms_netsim/add_custom_net_device "{dccommsId: 'node0', mac: 1, frameId: 'node0dev', maxBitRate: 2000, bitrate: 2000.27, bitrateSd: 0.0,
  maxDistance: 9000000, minDistance: 0, minPktErrorRate: 0.4, pktErrorRateIncPerMeter: 0.1}"
rosservice call /dccomms_netsim/add_custom_channel "id: 0
minPrTime: 0.0
prTimeIncPerMeter: 0.6"
rosservice call /dccomms_netsim/link_dev_to_channel "dccommsId: 'node0'
channelId: 0" 
rosservice call /dccomms_netsim/link_dev_to_channel "dccommsId: 'node1'
channelId: 0"
rosservice call /dccomms_netsim/start_simulation "{}"
