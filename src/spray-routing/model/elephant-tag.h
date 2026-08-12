#ifndef ELEPHANT_TAG_H
#define ELEPHANT_TAG_H

#include "ns3/tag.h"
#include "ns3/type-id.h"

namespace ns3 {

/**
 * \brief Packet tag used to identify packets belonging to an elephant flow.
 *
 * The simulation marks elephant traffic using IP TOS = 0x10.
 * ElephantTag provides an additional mechanism for identifying elephant
 * packets when required by the routing module.
 *
 * SprayRouting uses the elephant indication to select Packet Spraying;
 * non-elephant packets use conventional flow-based ECMP.
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
