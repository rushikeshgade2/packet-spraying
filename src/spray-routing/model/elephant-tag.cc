#include "elephant-tag.h"

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (ElephantTag);

TypeId
ElephantTag::GetTypeId ()
{
    static TypeId tid = TypeId ("ns3::ElephantTag")
        .SetParent<Tag> ()
        .SetGroupName ("SprayRouting")
        .AddConstructor<ElephantTag> ();
    return tid;
}

TypeId ElephantTag::GetInstanceTypeId () const { return GetTypeId (); }

uint32_t ElephantTag::GetSerializedSize () const { return sizeof (uint32_t); }

void ElephantTag::Serialize (TagBuffer buf) const   { buf.WriteU32 (m_flowId); }
void ElephantTag::Deserialize (TagBuffer buf)        { m_flowId = buf.ReadU32 (); }
void ElephantTag::Print (std::ostream &os) const     { os << "ElephantTag flowId=" << m_flowId; }

} // namespace ns3
