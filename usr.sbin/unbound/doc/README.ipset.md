## Created a module to support the ipset that could add the domain's ip to a list easily.

### Purposes:
* In my case, I can't access the facebook, twitter, youtube and thousands web site for some reason. VPN is a solution. But the internet too slow whether all traffics pass through the vpn.
So, I set up a transparent proxy to proxy the traffic which has been blocked only.
At the final step, I need to install a dns service which would work with ipset well to launch the system.
I did some research for this. Unfortunately, Unbound, My favorite dns service doesn't support ipset yet. So, I decided to implement it by my self and contribute the patch. It's good for me and the community.
```
# unbound.conf
server:
  ...
  local-zone: "facebook.com" ipset
  local-zone: "twitter.com" ipset
  local-zone: "instagram.com" ipset
  more social website

ipset:
  name-v4: "gfwlist"
```
```
# iptables
iptables -A PREROUTING -p tcp -m set --match-set gfwlist dst -j REDIRECT --to-ports 10800
iptables -A OUTPUT -p tcp -m set --match-set gfwlist dst -j REDIRECT --to-ports 10800
```

* This patch could work with iptables rules to batch block the IPs.
```
# unbound.conf
server:
  ...
  local-zone: "facebook.com" ipset
  local-zone: "twitter.com" ipset
  local-zone: "instagram.com" ipset
  more social website

ipset:
  name-v4: "blacklist"
  name-v6: "blacklist6"
```
```
# iptables
iptables -A INPUT -m set --set blacklist src -j DROP
ip6tables -A INPUT -m set --set blacklist6 src -j DROP
```

### Notes:
* To enable this module the root privileges is required.
* Please create a set with ipset command first. eg. **ipset -N blacklist iphash**

### How to use:
```
./configure --enable-ipset
make && make install
```

### Configuration:
```
# unbound.conf
server:
  ...
  local-zone: "example.com" ipset

ipset:
  name-v4: "blacklist"
```
