# The schema this generator is checked against, written the way schemas
# are written: no braces, no offsets. Indentation opens a block, and each
# field takes the next free byte after the one before it.
#
# It names every type the dialect has, so the header generated from it
# exercises every reader.
package echo

type id32 = bytes_fixed[32]

# What one peer says.
struct Ping
    # The sequence number the reply must carry back.
    Seq     u64
    Sender  id32
    Note    text
    Payload bytes
    Hops    u16
    Live    bool
    Drift   f64
    Trail   list<Ping>
    Origin  Site

# Where a message came from.
struct Site
    Host u32
    Port u16

# What the other peer says back.
struct Pong
    Seq   u64
    Delay f32

# Echo answers what it is told, and says how long it took.
interface Echo
    # Say something and wait to hear it back.
    ping(req: Ping) returns (resp: Pong)
    # Say something and do not wait.
    notify(req: Ping)
    # Ask whether the other side is there at all.
    health() returns (resp: Pong)
