#include "spray-routing-helper.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("SprayRoutingHelper");

SprayRoutingHelper::SprayRoutingHelper () = default;

SprayRoutingHelper *
SprayRoutingHelper::Copy () const
{
    return new SprayRoutingHelper (*this);
}

Ptr<Ipv4RoutingProtocol>
SprayRoutingHelper::Create (Ptr<Node> /*node*/) const
{
    Ptr<SprayRouting> r = CreateObject<SprayRouting> ();
    r->SetMode (m_sprayElephantOnly);
    return r;
}

} // namespace ns3
