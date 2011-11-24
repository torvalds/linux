cd ..\HostDriver
echo on
echo /I"..\HostDriver"                       >  ExtraOptions
echo /I"..\HostDriver\_win\inc"              >> ExtraOptions
echo /I"..\HostDriver\heap_win\inc"          >> ExtraOptions
echo /I"..\HostDriver\sys\inc"               >> ExtraOptions
echo /I"..\HostDriver\ucos\inc"              >> ExtraOptions
echo /I"..\HostDriver\ucos_port_winhost\inc" >> ExtraOptions
echo /I"..\HostDriver\buf\inc"               >> ExtraOptions
echo /I"..\HostDriver\s_buf\inc"             >> ExtraOptions
echo /I"..\HostDriver\bm_hw_addams\inc"      >> ExtraOptions
echo /I"..\HostDriver\wifi_drv\inc"          >> ExtraOptions
echo /I"..\HostDriver\reg\inc"               >> ExtraOptions
echo /I"..\HostDriver\mlme_proxy\inc"        >> ExtraOptions
echo /I"..\HostDriver\hic\inc"               >> ExtraOptions
echo /I"..\HostDriver\80211\inc"             >> ExtraOptions
echo /I"..\HostDriver\80211cbss\inc"         >> ExtraOptions
echo /I"..\HostDriver\80211macmgmt\inc"      >> ExtraOptions
echo /I"..\HostDriver\80211macmib\inc"       >> ExtraOptions
echo /I"..\HostDriver\80211macll\inc"        >> ExtraOptions
echo /I"..\HostDriver\80211macul\inc"        >> ExtraOptions
echo /I"..\HostDriver\str\inc"               >> ExtraOptions
echo /I"..\HostDriver\tsf_tmr\inc"           >> ExtraOptions
echo /I"..\HostDriver\bbrx\inc"              >> ExtraOptions
echo /I"..\HostDriver\driverenv\inc"         >> ExtraOptions