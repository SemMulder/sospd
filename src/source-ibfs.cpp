#include "flow-solver.hpp"

#include <iostream>
#include <limits>
#include <chrono>

#include "submodular-ibfs.hpp"

typedef std::chrono::system_clock::time_point TimePt;
typedef std::chrono::duration<double> Duration;
typedef std::chrono::system_clock Clock;

void SourceIBFS::IBFSInit()
{
    auto start = Clock::now();

    const int n = m_graph->NumNodes();

    m_source_layers = std::vector<NodeQueue>(n+1);

    m_source_orphans.clear();

    auto& nodes = m_graph->GetNodes();
    auto& sNode = nodes[m_graph->GetS()];
    sNode.state = NodeState::S;
    sNode.dis = 0;
    m_source_layers[0].push_back(sNode);
    auto& tNode = nodes[m_graph->GetT()];
    tNode.state = NodeState::T;
    tNode.dis = 0;

    // saturate all s-i-t paths
    for (NodeId i = 0; i < n; ++i) {
        REAL min_cap = std::min(m_graph->m_c_si[i]-m_graph->m_phi_si[i],
                m_graph->m_c_it[i]-m_graph->m_phi_it[i]);
        m_graph->m_phi_si[i] += min_cap;
        m_graph->m_phi_it[i] += min_cap;
        if (m_graph->m_c_si[i] > m_graph->m_phi_si[i]) {
            auto& node = m_graph->node(i);
            node.state = NodeState::S;
            node.dis = 1;
            AddToLayer(i);
            node.parent_arc = m_graph->ArcsEnd(i);
            node.parent = m_graph->GetS();
        } else if (m_graph->m_c_it[i] > m_graph->m_phi_it[i]) {
            auto& node = m_graph->node(i);
            node.state = NodeState::T;
            node.parent_arc = m_graph->ArcsEnd(i);
            node.parent = m_graph->GetT();
        } else {
            ASSERT(m_graph->m_c_si[i] == m_graph->m_phi_si[i] 
                && m_graph->m_c_it[i] == m_graph->m_phi_it[i]);
        }
    }
    m_initTime += Duration{ Clock::now() - start }.count();
}

void SourceIBFS::IBFS() {
    auto start = Clock::now();
    m_source_tree_d = 0;

    IBFSInit();

    // Set up initial current_q and search nodes to make it look like
    // we just finished scanning the source node
    NodeQueue* current_q = &(m_source_layers[0]);
    m_search_node_iter = current_q->end();
    m_search_node_end = current_q->end();

    while (!current_q->empty()) {
        if (m_search_node_iter == m_search_node_end) {
            // Swap queues and continue
            m_source_tree_d++;
            current_q = &(m_source_layers[m_source_tree_d]);
            m_search_node_iter = current_q->begin();
            m_search_node_end = current_q->end();
            if (!current_q->empty()) {
                Node& n = *m_search_node_iter;
                NodeId nodeIdx = n.id;
                ASSERT(n.state == NodeState::S);
                m_search_arc = m_graph->ArcsBegin(nodeIdx);
                m_search_arc_end = m_graph->ArcsEnd(nodeIdx);
            }
            continue;
        }
        Node& n = *m_search_node_iter;
        NodeId search_node = n.id;
        int distance = m_source_tree_d;
        ASSERT(n.dis == distance);
        // Advance m_search_arc until we find a residual arc
        while (m_search_arc != m_search_arc_end && !m_graph->NonzeroCap(m_search_arc, true))
            ++m_search_arc;

        if (m_search_arc != m_search_arc_end) {
            NodeId neighbor = m_search_arc.Target();
            NodeState neighbor_state = m_graph->node(neighbor).state;
            if (neighbor_state == n.state) {
                ASSERT(m_graph->node(neighbor).dis <= n.dis + 1);
                if (m_graph->node(neighbor).dis == n.dis+1) {
                    auto reverseArc = m_search_arc.Reverse();
                    if (reverseArc < m_graph->node(neighbor).parent_arc) {
                        m_graph->node(neighbor).parent_arc = reverseArc;
                        m_graph->node(neighbor).parent = search_node;
                    }
                }
                ++m_search_arc;
            } else if (neighbor_state == NodeState::N) {
                // Then we found an unlabeled node, add it to the tree
                m_graph->node(neighbor).state = n.state;
                m_graph->node(neighbor).dis = n.dis + 1;
                AddToLayer(neighbor);
                auto reverseArc = m_search_arc.Reverse();
                m_graph->node(neighbor).parent_arc = reverseArc;
                ASSERT(m_graph->NonzeroCap(m_graph->node(neighbor).parent_arc, false));
                m_graph->node(neighbor).parent = search_node;
                ++m_search_arc;
            } else {
                // Then we found an arc to the other tree
                ASSERT(neighbor_state == NodeState::T);
                ASSERT(m_graph->NonzeroCap(m_search_arc, true));
                Augment(m_search_arc);
                Adopt();
            }
        } else {
            // No more arcs to scan from this node, so remove from queue
            AdvanceSearchNode();
        }
    } // End while
    m_totalTime += Duration{ Clock::now() - start }.count();

    //std::cout << "Total time:      " << m_totalTime << "\n";
    //std::cout << "Init time:       " << m_initTime << "\n";
    //std::cout << "Augment time:    " << m_augmentTime << "\n";
    //std::cout << "Adopt time:      " << m_adoptTime << "\n";
}

void SourceIBFS::Augment(ArcIterator& arc) {
    auto start = Clock::now();

    NodeId i, j;
    i = arc.Source();
    j = arc.Target();
    REAL bottleneck = m_graph->ResCap(arc, true);
    NodeId current = i;
    NodeId parent = m_graph->node(current).parent;
    while (parent != m_graph->GetS()) {
        ASSERT(m_graph->node(current).state == NodeState::S);
        auto& a = m_graph->node(current).parent_arc;
        bottleneck = std::min(bottleneck, m_graph->ResCap(a, false));
        current = parent;
        parent = m_graph->node(current).parent;
    }
    ASSERT(m_graph->node(current).parent == m_graph->GetS());
    bottleneck = std::min(bottleneck, m_graph->m_c_si[current] - m_graph->m_phi_si[current]);

    current = j;
    ASSERT(m_graph->node(current).parent == m_graph->GetT());
    bottleneck = std::min(bottleneck, m_graph->m_c_it[current] - m_graph->m_phi_it[current]);
    ASSERT(bottleneck > 0);

    // Found the bottleneck, now do pushes on the arcs in the path
    Push(arc, true, bottleneck);
    current = i;
    parent = m_graph->node(current).parent;
    while (parent != m_graph->GetS()) {
        auto& a = m_graph->node(current).parent_arc;
        Push(a, false, bottleneck);
        current = parent;
        parent = m_graph->node(current).parent;
    }
    ASSERT(m_graph->node(current).parent == m_graph->GetS());
    m_graph->m_phi_si[current] += bottleneck;
    if (m_graph->m_phi_si[current] == m_graph->m_c_si[current])
        MakeOrphan(current);

    current = j;
    ASSERT(m_graph->node(current).parent == m_graph->GetT());
    m_graph->m_phi_it[current] += bottleneck;
    if (m_graph->m_phi_it[current] == m_graph->m_c_it[current])
        MakeOrphan(current);

    m_augmentTime += Duration{ Clock::now() - start }.count();
}

void SourceIBFS::Adopt() {
    auto start = Clock::now();
    while (!m_source_orphans.empty()) {
        Node& n = m_source_orphans.front();
        NodeId i = n.id;
        m_source_orphans.pop_front();
        int old_dist = n.dis;
        while (n.parent_arc != m_graph->ArcsEnd(i)
                && (m_graph->node(n.parent).state == NodeState::T
                    || m_graph->node(n.parent).state == NodeState::N
                    || m_graph->node(n.parent).dis != old_dist - 1
                    || !m_graph->NonzeroCap(n.parent_arc, false))) {
            ++n.parent_arc;
            if (n.parent_arc != m_graph->ArcsEnd(i))
                n.parent = n.parent_arc.Target();
        }
        if (n.parent_arc == m_graph->ArcsEnd(i)) {
            RemoveFromLayer(i);
            // We didn't find a new parent with the same label, so do a relabel
            n.dis = std::numeric_limits<int>::max()-1;
            for (auto newParentArc = m_graph->ArcsBegin(i); newParentArc != m_graph->ArcsEnd(i); ++newParentArc) {
                auto target = newParentArc.Target();
                if (m_graph->node(target).dis < n.dis
                        && (m_graph->node(target).state == NodeState::S
                            || m_graph->node(target).state == NodeState::S_orphan)
                        && m_graph->NonzeroCap(newParentArc, false)) {
                    n.dis = m_graph->node(target).dis;
                    n.parent_arc = newParentArc;
                    ASSERT(m_graph->NonzeroCap(n.parent_arc, false));
                    n.parent = target;
                }
            }
            n.dis++;
            int cutoff_distance = m_source_tree_d + 1;
            if (n.dis > cutoff_distance) {
                n.state = NodeState::N;
            } else {
                n.state = NodeState::S;
                AddToLayer(i);
            }
            // FIXME(afix) Should really assert that n.dis > old_dis
            // but current-arc heuristic isn't watertight at the moment...
            if (n.dis > old_dist) {
                for (auto arc = m_graph->ArcsBegin(i); arc != m_graph->ArcsEnd(i); ++arc) {
                    if (m_graph->node(arc.Target()).parent == i)
                        MakeOrphan(arc.Target());
                }
            }
        } else {
            ASSERT(m_graph->NonzeroCap(n.parent_arc, false));
            n.state = NodeState::S;
        }
    }
    m_adoptTime += Duration{ Clock::now() - start }.count();
}

void SourceIBFS::MakeOrphan(NodeId i) {
    Node& n = m_graph->node(i);
    if (n.state != NodeState::S && n.state != NodeState::T)
        return;
    if (n.state == NodeState::S) {
        n.state = NodeState::S_orphan;
        m_source_orphans.push_back(n);
    } else if (n.state == NodeState::T) {
        n.state = NodeState::N;
    }
}


void SourceIBFS::Push(ArcIterator& arc, bool forwardArc, REAL delta) {
    ASSERT(delta > 0);
    //ASSERT(delta > -1e-7);//Chen
    m_num_clique_pushes++;
    //std::cout << "Pushing on clique arc (" << arc.i << ", " << arc.j << ") -- delta = " << delta << std::endl;
    auto& c = m_graph->clique(arc.cliqueId());
    if (forwardArc)
        c.Push(arc.SourceIdx(), arc.TargetIdx(), delta);
    else
        c.Push(arc.TargetIdx(), arc.SourceIdx(), delta);
    for (NodeId n : c.Nodes()) {
        if (m_graph->node(n).state == NodeState::N)
            continue;
        auto& parent_arc = m_graph->node(n).parent_arc;
        if (parent_arc != m_graph->ArcsEnd(n) && parent_arc.cliqueId() == arc.cliqueId() && !m_graph->NonzeroCap(parent_arc, m_graph->node(n).state == NodeState::T)) {
            MakeOrphan(n);
        }
    }
}

/////////////// end of push relabel ///////////////////


void SourceIBFS::ComputeMinCut() {
    auto& labels = m_energy->GetLabels();
    for (NodeId i = 0; i < m_graph->NumNodes(); ++i) {
        if (m_graph->node(i).state == NodeState::T)
            labels[i] = 0;
        else if (m_graph->node(i).state == NodeState::S)
            labels[i] = 1;
        else {
            ASSERT(m_graph->node(i).state == NodeState::N);
            // Put N nodes on whichever side could still grow
            labels[i] = 0;
        }
    }
}

void SourceIBFS::Solve(SubmodularIBFS* energy) {
    m_energy = energy;
    m_graph = &energy->Graph();
    m_graph->ResetFlow();
    m_graph->UpperBoundCliques(energy->Params().ub, energy->Params().fixedVars, energy->GetLabels(), energy->NormStats());
    IBFS();
    ComputeMinCut();
}

void SourceIBFS::AddToLayer(NodeId i) {
    auto& node = m_graph->node(i);
    int dis = node.dis;
    if (node.state == NodeState::S) {
        m_source_layers[dis].push_back(node);
        if (node.dis == m_source_tree_d)
            m_search_node_end = m_source_layers[m_source_tree_d].end();
    } else {
        ASSERT(false);
    }
}

void SourceIBFS::RemoveFromLayer(NodeId i) {
    auto& node = m_graph->node(i);
    if (m_search_node_iter != m_search_node_end && &(*m_search_node_iter) == &node)
        AdvanceSearchNode();
    int dis = node.dis;
    if (node.state == NodeState::S || node.state == NodeState::S_orphan) {
        auto& layer = m_source_layers[dis];
        layer.erase(layer.iterator_to(node));
    } else {
        ASSERT(false);
    }
}

void SourceIBFS::AdvanceSearchNode() {
    m_search_node_iter++;
    if (m_search_node_iter != m_search_node_end) {
        Node& n = *m_search_node_iter;
        NodeId i = n.id;
        ASSERT(n.state == NodeState::S || n.state == NodeState::S_orphan);
        m_search_arc = m_graph->ArcsBegin(i);
        m_search_arc_end = m_graph->ArcsEnd(i);
    }
}
