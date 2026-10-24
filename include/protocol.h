#ifndef BUTTERFLOW_PROTOCOL_H
#define BUTTERFLOW_PROTOCOL_H

#include <cstdint>

#pragma pack(push, 1)

// Butterflow Magic Signature: "BTFL" (0x4C465442 in LE)
constexpr uint32_t BUTTERFLOW_MAGIC = 0x4C465442;
constexpr uint16_t BUTTERFLOW_PORT = 54823;
constexpr uint8_t  BUTTERFLOW_VERSION = 1;

enum PacketType : uint8_t {
    PKT_DISCOVERY_BEACON = 0x01,
    PKT_DISCOVERY_RESPONSE = 0x02,
    PKT_HANDSHAKE_REQUEST = 0x03,
    PKT_HANDSHAKE_ACCEPT = 0x04,
    
    // Apple-style Edge Affordance & Transition
    PKT_EDGE_APPROACH = 0x10,      // Cursor pushing against boundary (0.0 -> 1.0 progress)
    PKT_CURSOR_ENTER = 0x11,       // Cursor has transitioned onto this screen
    PKT_CURSOR_LEAVE = 0x12,       // Cursor has transitioned back off this screen
    
    // High-Frequency Real-time Input (UDP)
    PKT_INPUT_MOUSE = 0x20,
    PKT_INPUT_KEYBOARD = 0x21,
    
    // Clipboard & State
    PKT_CLIPBOARD_NOTIFY = 0x30,
    PKT_CLIPBOARD_DATA = 0x31,
    
    // Liveness
    PKT_HEARTBEAT = 0x40,
    PKT_DISCONNECT = 0x41
};

enum ScreenEdge : uint8_t {
    EDGE_LEFT = 0,
    EDGE_RIGHT = 1,
    EDGE_TOP = 2,
    EDGE_BOTTOM = 3
};

// Base header for all packets
struct PacketHeader {
    uint32_t magic;
    uint8_t  version;
    uint8_t  type;
    uint16_t payloadSize;
    uint32_t sequence;
};

// Auto-discovery beacon
struct DiscoveryPayload {
    char     deviceName[32];
    uint16_t screenWidth;
    uint16_t screenHeight;
    uint8_t  preferredEdge; // Which side this screen expects the peer on
};

// Edge resistance & visual bubble indicator
struct EdgeApproachPayload {
    float    pushProgress;   // 0.0f (just touched) to 1.0f (threshold reached)
    float    normalizedY;    // Vertical position (0.0f to 1.0f)
    uint8_t  edge;           // Target edge on receiving screen
};

// Cursor handoff
struct CursorTransitionPayload {
    float    normalizedX;    // 0.0f to 1.0f
    float    normalizedY;    // 0.0f to 1.0f
    uint8_t  edge;
};

// High-speed mouse input packet
struct MouseInputPayload {
    uint16_t flags;          // Button states, move flags
    int32_t  dx;             // Relative delta X
    int32_t  dy;             // Relative delta Y
    uint16_t normX;          // Normalized X (0 - 65535)
    uint16_t normY;          // Normalized Y (0 - 65535)
    int16_t  wheelDelta;     // Vertical scroll wheel
    int16_t  hWheelDelta;    // Horizontal scroll tilt
};

// Keyboard input packet
struct KeyboardInputPayload {
    uint16_t vkCode;         // Virtual-key code
    uint16_t scanCode;       // Hardware scan code
    uint32_t flags;          // KEYEVENTF_KEYUP, KEYEVENTF_EXTENDEDKEY, etc.
};

// Clipboard notify
struct ClipboardNotifyPayload {
    uint32_t format;         // CF_UNICODETEXT, CF_DIB, etc.
    uint32_t dataLength;
};

#pragma pack(pop)

// Mouse Flag Definitions
constexpr uint16_t MFLAG_MOVE        = 0x0001;
constexpr uint16_t MFLAG_LEFT_DOWN   = 0x0002;
constexpr uint16_t MFLAG_LEFT_UP     = 0x0004;
constexpr uint16_t MFLAG_RIGHT_DOWN  = 0x0008;
constexpr uint16_t MFLAG_RIGHT_UP    = 0x0010;
constexpr uint16_t MFLAG_MID_DOWN    = 0x0020;
constexpr uint16_t MFLAG_MID_UP      = 0x0040;
constexpr uint16_t MFLAG_WHEEL       = 0x0080;
constexpr uint16_t MFLAG_HWHEEL      = 0x0100;
constexpr uint16_t MFLAG_X1_DOWN     = 0x0200;
constexpr uint16_t MFLAG_X1_UP       = 0x0400;
constexpr uint16_t MFLAG_X2_DOWN     = 0x0800;
constexpr uint16_t MFLAG_X2_UP       = 0x1000;

#endif // BUTTERFLOW_PROTOCOL_H
