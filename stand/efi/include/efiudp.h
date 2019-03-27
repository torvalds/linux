/* $FreeBSD$ */
#ifndef _EFI_UDP_H
#define _EFI_UDP_H


/*++
Copyright (c) 2013  Intel Corporation

--*/

#define EFI_UDP4_SERVICE_BINDING_PROTOCOL \
    { 0x83f01464, 0x99bd, 0x45e5, {0xb3, 0x83, 0xaf, 0x63, 0x05, 0xd8, 0xe9, 0xe6} }

#define EFI_UDP4_PROTOCOL \
    { 0x3ad9df29, 0x4501, 0x478d, {0xb1, 0xf8, 0x7f, 0x7f, 0xe7, 0x0e, 0x50, 0xf3} }

#define EFI_UDP6_SERVICE_BINDING_PROTOCOL \
    { 0x66ed4721, 0x3c98, 0x4d3e, {0x81, 0xe3, 0xd0, 0x3d, 0xd3, 0x9a, 0x72, 0x54} }

#define EFI_UDP6_PROTOCOL \
    { 0x4f948815, 0xb4b9, 0x43cb, {0x8a, 0x33, 0x90, 0xe0, 0x60, 0xb3,0x49, 0x55} }

INTERFACE_DECL(_EFI_UDP4);
INTERFACE_DECL(_EFI_UDP6);

typedef struct {
    BOOLEAN          AcceptBroadcast;
    BOOLEAN          AcceptPromiscuous;
    BOOLEAN          AcceptAnyPort;
    BOOLEAN          AllowDuplicatePort;
    UINT8            TypeOfService;
    UINT8            TimeToLive;
    BOOLEAN          DoNotFragment;
    UINT32           ReceiveTimeout;
    UINT32           TransmitTimeout;
    BOOLEAN          UseDefaultAddress;
    EFI_IPv4_ADDRESS StationAddress;
    EFI_IPv4_ADDRESS SubnetMask;
    UINT16           StationPort;
    EFI_IPv4_ADDRESS RemoteAddress;
    UINT16           RemotePort;
} EFI_UDP4_CONFIG_DATA;

typedef
EFI_STATUS
(EFIAPI *EFI_UDP4_GET_MODE_DATA) (
    IN struct _EFI_UDP4                 *This,
    OUT EFI_UDP4_CONFIG_DATA            *Udp4ConfigData OPTIONAL,
    OUT EFI_IP4_MODE_DATA               *Ip4ModeData    OPTIONAL,
    OUT EFI_MANAGED_NETWORK_CONFIG_DATA *MnpConfigData  OPTIONAL,
    OUT EFI_SIMPLE_NETWORK_MODE         *SnpModeData    OPTIONAL
    );

typedef
EFI_STATUS
(EFIAPI *EFI_UDP4_CONFIGURE) (
    IN struct _EFI_UDP4     *This,
    IN EFI_UDP4_CONFIG_DATA *UdpConfigData OPTIONAL
    );

typedef
EFI_STATUS
(EFIAPI *EFI_UDP4_GROUPS) (
    IN struct _EFI_UDP4 *This,
    IN BOOLEAN          JoinFlag,
    IN EFI_IPv4_ADDRESS *MulticastAddress OPTIONAL
    );

typedef
EFI_STATUS
(EFIAPI *EFI_UDP4_ROUTES) (
    IN struct _EFI_UDP4 *This,
    IN BOOLEAN          DeleteRoute,
    IN EFI_IPv4_ADDRESS *SubnetAddress,
    IN EFI_IPv4_ADDRESS *SubnetMask,
    IN EFI_IPv4_ADDRESS *GatewayAddress
    );

#define EFI_NETWORK_UNREACHABLE  EFIERR(100)
#define EFI_HOST_UNREACHABLE     EFIERR(101)
#define EFI_PROTOCOL_UNREACHABLE EFIERR(102)
#define EFI_PORT_UNREACHABLE     EFIERR(103)

typedef struct {
    EFI_IPv4_ADDRESS SourceAddress;
    UINT16           SourcePort;
    EFI_IPv4_ADDRESS DestinationAddress;
    UINT16           DestinationPort;
} EFI_UDP4_SESSION_DATA;

typedef struct {
    UINT32 FragmentLength;
    VOID   *FragmentBuffer;
} EFI_UDP4_FRAGMENT_DATA;

typedef struct {
    EFI_TIME               TimeStamp;
    EFI_EVENT              RecycleSignal;
    EFI_UDP4_SESSION_DATA  UdpSession;
    UINT32                 DataLength;
    UINT32                 FragmentCount;
    EFI_UDP4_FRAGMENT_DATA FragmentTable[1];
} EFI_UDP4_RECEIVE_DATA;

typedef struct {
    EFI_UDP4_SESSION_DATA  *UdpSessionData;
    EFI_IPv4_ADDRESS       *GatewayAddress;
    UINT32                 DataLength;
    UINT32                 FragmentCount;
    EFI_UDP4_FRAGMENT_DATA FragmentTable[1];
} EFI_UDP4_TRANSMIT_DATA;

typedef struct {
    EFI_EVENT                  Event;
    EFI_STATUS                 Status;
    union {
        EFI_UDP4_RECEIVE_DATA  *RxData;
	EFI_UDP4_TRANSMIT_DATA *TxData;
    }                          Packet;
} EFI_UDP4_COMPLETION_TOKEN;

typedef
EFI_STATUS
(EFIAPI *EFI_UDP4_TRANSMIT) (
    IN struct _EFI_UDP4          *This,
    IN EFI_UDP4_COMPLETION_TOKEN *Token
    );

typedef
EFI_STATUS
(EFIAPI *EFI_UDP4_RECEIVE) (
    IN struct _EFI_UDP4          *This,
    IN EFI_UDP4_COMPLETION_TOKEN *Token
    );

typedef
EFI_STATUS
(EFIAPI *EFI_UDP4_CANCEL)(
    IN struct _EFI_UDP4          *This,
    IN EFI_UDP4_COMPLETION_TOKEN *Token OPTIONAL
    );

typedef
EFI_STATUS
(EFIAPI *EFI_UDP4_POLL) (
    IN struct _EFI_UDP4 *This
    );

typedef struct _EFI_UDP4 {
    EFI_UDP4_GET_MODE_DATA GetModeData;
    EFI_UDP4_CONFIGURE     Configure;
    EFI_UDP4_GROUPS        Groups;
    EFI_UDP4_ROUTES        Routes;
    EFI_UDP4_TRANSMIT      Transmit;
    EFI_UDP4_RECEIVE       Receive;
    EFI_UDP4_CANCEL        Cancel;
    EFI_UDP4_POLL          Poll;
} EFI_UDP4;

typedef struct {
    BOOLEAN          AcceptPromiscuous;
    BOOLEAN          AcceptAnyPort;
    BOOLEAN          AllowDuplicatePort;
    UINT8            TrafficClass;
    UINT8            HopLimit;
    UINT32           ReceiveTimeout;
    UINT32           TransmitTimeout;
    EFI_IPv6_ADDRESS StationAddress;
    UINT16           StationPort;
    EFI_IPv6_ADDRESS RemoteAddress;
    UINT16           RemotePort;
} EFI_UDP6_CONFIG_DATA;

typedef
EFI_STATUS
(EFIAPI *EFI_UDP6_GET_MODE_DATA) (
    IN struct _EFI_UDP6                 *This,
    OUT EFI_UDP6_CONFIG_DATA            *Udp6ConfigData OPTIONAL,
    OUT EFI_IP6_MODE_DATA               *Ip6ModeData    OPTIONAL,
    OUT EFI_MANAGED_NETWORK_CONFIG_DATA *MnpConfigData  OPTIONAL,
    OUT EFI_SIMPLE_NETWORK_MODE         *SnpModeData    OPTIONAL
    );

typedef
EFI_STATUS
(EFIAPI *EFI_UDP6_CONFIGURE) (
    IN struct _EFI_UDP6     *This,
    IN EFI_UDP6_CONFIG_DATA *UdpConfigData OPTIONAL
    );

typedef
EFI_STATUS
(EFIAPI *EFI_UDP6_GROUPS) (
    IN struct _EFI_UDP6 *This,
    IN BOOLEAN          JoinFlag,
    IN EFI_IPv6_ADDRESS *MulticastAddress OPTIONAL
    );

typedef struct {
    EFI_IPv6_ADDRESS SourceAddress;
    UINT16           SourcePort;
    EFI_IPv6_ADDRESS DestinationAddress;
    UINT16           DestinationPort;
} EFI_UDP6_SESSION_DATA;

typedef struct {
    UINT32 FragmentLength;
    VOID   *FragmentBuffer;
} EFI_UDP6_FRAGMENT_DATA;

typedef struct {
    EFI_TIME               TimeStamp;
    EFI_EVENT              RecycleSignal;
    EFI_UDP6_SESSION_DATA  UdpSession;
    UINT32                 DataLength;
    UINT32                 FragmentCount;
    EFI_UDP6_FRAGMENT_DATA FragmentTable[1];
} EFI_UDP6_RECEIVE_DATA;

typedef struct {
    EFI_UDP6_SESSION_DATA  *UdpSessionData;
    UINT32                 DataLength;
    UINT32                 FragmentCount;
    EFI_UDP6_FRAGMENT_DATA FragmentTable[1];
} EFI_UDP6_TRANSMIT_DATA;

typedef struct {
    EFI_EVENT                  Event;
    EFI_STATUS                 Status;
    union {
        EFI_UDP6_RECEIVE_DATA  *RxData;
        EFI_UDP6_TRANSMIT_DATA *TxData;
    }                          Packet;
} EFI_UDP6_COMPLETION_TOKEN;

typedef
EFI_STATUS
(EFIAPI *EFI_UDP6_TRANSMIT) (
    IN struct _EFI_UDP6          *This,
    IN EFI_UDP6_COMPLETION_TOKEN *Token
    );

typedef
EFI_STATUS
(EFIAPI *EFI_UDP6_RECEIVE) (
    IN struct _EFI_UDP6          *This,
    IN EFI_UDP6_COMPLETION_TOKEN *Token
    );

typedef
EFI_STATUS
(EFIAPI *EFI_UDP6_CANCEL)(
    IN struct _EFI_UDP6          *This,
    IN EFI_UDP6_COMPLETION_TOKEN *Token OPTIONAL
    );

typedef
EFI_STATUS
(EFIAPI *EFI_UDP6_POLL) (
    IN struct _EFI_UDP6 *This
    );

typedef struct _EFI_UDP6 {
    EFI_UDP6_GET_MODE_DATA GetModeData;
    EFI_UDP6_CONFIGURE     Configure;
    EFI_UDP6_GROUPS        Groups;
    EFI_UDP6_TRANSMIT      Transmit;
    EFI_UDP6_RECEIVE       Receive;
    EFI_UDP6_CANCEL        Cancel;
    EFI_UDP6_POLL          Poll;
} EFI_UDP6;

#endif /* _EFI_UDP_H */
