rosservice call /dccomms_netsim/add_channel "id: 0                                                                                         
type: 0"

rosservice call /dccomms_netsim/add_net_device "{dccommsId: 'node0', type: 0, mac: 1, frameId: 'node0', maxBitRate: 1200, energyModel: 0}"
rosservice call /dccomms_netsim/link_dev_to_channel "dccommsId: 'node0'
channelId: 0"

rosservice call /dccomms_netsim/add_net_device "{dccommsId: 'node1', type: 0, mac: 2, frameId: 'node1', maxBitRate: 1200, energyModel: 0}"
rosservice call /dccomms_netsim/link_dev_to_channel "dccommsId: 'node1'
channelId: 0"

rosservice call /dccomms_netsim/start_simulation "{}"
