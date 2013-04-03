#include "alpha-expansion.hpp"

MultiLabelCRF::MultiLabelCRF(Label max_label)
    : m_num_labels(max_label),
    m_constant_term(0),
    m_cliques(),
    m_unary_cost(),
    m_labels()
{ }

MultiLabelCRF::NodeId MultiLabelCRF::AddNode(int n) {
    NodeId ret = m_labels.size();
    for (int i = 0; i < n; ++i) {
        m_labels.push_back(0);
        UnaryCost uc(m_num_labels, 0);
        m_unary_cost.push_back(uc);
    }
    return ret;
}

int MultiLabelCRF::GetLabel(NodeId i) const {
    return m_labels[i];
}

void MultiLabelCRF::AddConstantTerm(REAL c) {
    m_constant_term += c;
}

void MultiLabelCRF::AddUnaryTerm(NodeId i, const std::vector<REAL>& coeffs) {
    ASSERT(coeffs.size() == m_num_labels);
    for (size_t j = 0; j < m_num_labels; ++j) {
        m_unary_cost[i][j] += coeffs[j];
    }
}

void MultiLabelCRF::AddClique(const CliquePtr& cp) {
    m_cliques.push_back(cp);
}

void MultiLabelCRF::SetupAlphaEnergy(Label alpha, SubmodularFlow& crf) const {
    typedef int32_t Assgn;
    for (const CliquePtr& cp : m_cliques) {
        const Clique& c = *cp;
        const size_t k = c.Size();
        ASSERT(k < 32);
        const Assgn max_assgn = 1 << k;
        std::vector<REAL> energy_table;
        std::vector<Label> label_buf;
        for (Assgn a = 0; a < max_assgn; ++a) {
            label_buf.clear();
            for (size_t i_idx = 0; i_idx < k; ++i_idx) {
                if (a & (1 << i_idx))
                    label_buf.push_back(alpha);
                else
                    label_buf.push_back(m_labels[c.Nodes()[i_idx]]);
            }
            energy_table.push_back(c.Energy(label_buf));
        }
        crf.AddClique(c.Nodes(), energy_table);
    }
}

void MultiLabelCRF::AlphaExpansion() {
    std::cout << "Starting Alpha Expansion\n";
    REAL last_energy = std::numeric_limits<REAL>::max();
    REAL energy = ComputeEnergy();
    const NodeId n = m_labels.size();
    size_t num_rounds = 0;
    while (energy < last_energy) {
        std::cout << "\tRound: " << num_rounds;
        std::cout.flush();
        for (Label alpha = 0; alpha < m_num_labels; ++alpha) {
            std::cout << ".";
            std::cout.flush();
            SubmodularFlow crf;
            crf.AddNode(m_labels.size());
            SetupAlphaEnergy(alpha, crf);
            crf.Solve();
            for (NodeId i = 0; i < n; ++i) {
                int crf_label = crf.GetLabel(i);
                if (crf_label == 1)
                    m_labels[i] = alpha;
            }
        }
        std::cout << "\n";
        last_energy = energy;
        energy = ComputeEnergy();
        num_rounds++;
    }
}

void MultiLabelCRF::Solve() {
    AlphaExpansion();
}

REAL MultiLabelCRF::ComputeEnergy() const {
    return ComputeEnergy(m_labels);
}

REAL MultiLabelCRF::ComputeEnergy(const std::vector<Label>& labels) const {
    REAL energy = m_constant_term;
    std::vector<Label> labelBuf;
    for (const CliquePtr& cp : m_cliques) {
        const Clique& c = *cp;
        labelBuf.clear();
        for (NodeId i : c.Nodes()) 
            labelBuf.push_back(m_labels[i]);
        energy += c.Energy(labelBuf);
    }
    return energy;
}

