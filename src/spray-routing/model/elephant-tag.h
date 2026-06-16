#ifndef ELEPHANT_TAG_H
#define ELEPHANT_TAG_H

#include "ns3/tag.h"
#include "ns3/type-id.h"

namespace ns3 {

/**
 * \brief Packet tag that marks a packet as belonging to an elephant flow.
 *
 * Added by the source application when the flow size exceeds the
 * elephant threshold (default 1 MB).  The SprayRouting module reads
 * this tag to decide whether to spray the packet across all equal-cost
 * paths (elephant) or pick a single path via hash (mouse).
 */
class ElephantTag : public Tag
{
public:
    static TypeId GetTypeId ();
    TypeId GetInstanceTypeId () const override;

    uint32_t GetSerializedSize () const override;
    void     Serialize   (TagBuffer buf) const override;
    void     Deserialize (TagBuffer buf) override;
    void     Print       (std::ostream &os) const override;

    void     SetFlowId (uint32_t id) { m_flowId = id; }
    uint32_t GetFlowId () const      { return m_flowId; }

private:
    uint32_t m_flowId{0};
};

} // namespace ns3
#endif // ELEPHANT_TAG_H
