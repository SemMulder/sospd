#ifndef _CONTRAST_SUBMODULAR_FEATURE_HPP_
#define _CONTRAST_SUBMODULAR_FEATURE_HPP_

#include "interactive_seg_app.hpp"
#include "feature.hpp"
#include <opencv2/core/core.hpp>
#include <boost/serialization/access.hpp>
#include <boost/serialization/base_object.hpp>
#include <boost/serialization/export.hpp>

class ContrastSubmodularFeature : public InteractiveSegApp::FG {
    public: 
    typedef InteractiveSegApp::FG::Constr Constr;
    typedef std::function<void(const std::vector<unsigned char>&)> PatchFn;
    typedef uint32_t Assgn;

    static constexpr Assgn clique_size = 4;
    static constexpr size_t per_cluster = (1 << clique_size) - 2;
    static constexpr size_t num_clusters = 50;
    double m_scale;

    ContrastSubmodularFeature() : m_scale(1.0) { }
    explicit ContrastSubmodularFeature(double scale) : m_scale(scale) { }

    virtual size_t NumFeatures() const override { return per_cluster*num_clusters; }
    virtual std::vector<FVAL> Psi(const IS_PatternData& p, const IS_LabelData& l) const override {
        const cv::Mat& patch_feature = m_patch_feature[p.Name()];
        const Assgn all_zeros = 0;
        const Assgn all_ones = (1 << clique_size) - 1;
        std::vector<FVAL> psi(NumFeatures(), 0);
        cv::Point base, pt;
        const cv::Point patch_size(1.0, 1.0);
        for (base.y = 0; base.y + patch_size.y < p.m_image.rows; ++base.y) {
            for (base.x = 0; base.x + patch_size.x < p.m_image.cols; ++base.x) {
                Assgn a = 0;
                int feature = patch_feature.at<int>(base);
                int i = 0;
                for (pt.y = base.y; pt.y <= base.y + patch_size.y; ++pt.y) {
                    for (pt.x = base.x; pt.x <= base.x + patch_size.x; ++pt.x) {
                        unsigned char label = l.m_gt.at<unsigned char>(pt);
                        if (label == cv::GC_FGD || label == cv::GC_PR_FGD)
                            a |= 1 << i;
                        i++;
                    }
                }
                if (a != all_zeros && a != all_ones)
                    psi[a-1 + feature*per_cluster] += m_scale;
            }
        }
        for (auto& v : psi)
            v = -v;
        return psi;
    }
    virtual void AddToCRF(CRF& crf, const IS_PatternData& p, double* w) const override {
        const cv::Mat& patch_feature = m_patch_feature[p.Name()];
        std::vector<std::vector<REAL>> costTables(num_clusters, std::vector<REAL>(per_cluster+2, 0));
        for (size_t i = 0; i < num_clusters; ++i) {
            for (size_t j = 0; j < per_cluster; ++j) {
                costTables[i][j+1] = doubleToREAL(m_scale*w[i*per_cluster+j]);
            }
        }
        cv::Point base, pt;
        const cv::Point patch_size(1.0, 1.0);
        for (base.y = 0; base.y + patch_size.y < p.m_image.rows; ++base.y) {
            for (base.x = 0; base.x + patch_size.x < p.m_image.cols; ++base.x) {
                std::vector<CRF::NodeId> vars;
                int feature = patch_feature.at<int>(base);
                for (pt.y = base.y; pt.y <= base.y + patch_size.y; ++pt.y) {
                    for (pt.x = base.x; pt.x <= base.x + patch_size.x; ++pt.x) {
                        vars.push_back(pt.y*p.m_image.cols + pt.x);
                    }
                }
                crf.AddClique(vars, costTables[feature]);
            }
        }
    }
    virtual Constr CollectConstrs(size_t feature_base, double constraint_scale) const override {
        typedef std::vector<std::pair<size_t, double>> LHS;
        typedef double RHS;
        Constr ret;
        Assgn all_zeros = 0;
        Assgn all_ones = (1 << clique_size) - 1;
        for (size_t cluster = 0; cluster < num_clusters; ++cluster) {
            size_t base = feature_base + cluster*per_cluster;
            for (Assgn s = 0; s < all_ones; ++s) {
                for (size_t i = 0; i < clique_size; ++i) {
                    Assgn si = s | (1 << i);
                    if (si != s) {
                        for (size_t j = i+1; j < clique_size; ++j) {
                            Assgn t = s | (1 << j);
                            if (t != s && j != i) {
                                Assgn ti = t | (1 << i);
                                // Decreasing marginal costs, so we require
                                // f(ti) - f(t) <= f(si) - f(s)
                                // i.e. f(si) - f(s) - f(ti) + f(t) >= 0
                                LHS lhs = {{base+si-1, constraint_scale}, {base+t-1, constraint_scale}};
                                if (s != all_zeros) lhs.push_back(std::make_pair(base+s-1, -constraint_scale));
                                if (ti != all_ones) lhs.push_back(std::make_pair(base+ti-1, -constraint_scale));
                                RHS rhs = 0;
                                ret.push_back(std::make_pair(lhs, rhs));
                            }
                        }
                    }
                }
            }
        }
        return ret;
    }
    virtual double Violation(size_t feature_base, double* w) const override {
        size_t num_constraints = 0;
        double total_violation = 0;
        Assgn all_zeros = 0;
        Assgn all_ones = (1 << clique_size) - 1;
        for (size_t cluster = 0; cluster < num_clusters; ++cluster) {
            size_t base = feature_base + cluster*per_cluster;
            for (Assgn s = 0; s < all_ones; ++s) {
                for (size_t i = 0; i < clique_size; ++i) {
                    Assgn si = s | (1 << i);
                    if (si != s) {
                        for (size_t j = i+1; j < clique_size; ++j) {
                            Assgn t = s | (1 << j);
                            if (t != s && j != i) {
                                Assgn ti = t | (1 << i);
                                num_constraints++;
                                // Decreasing marginal costs, so we require
                                // f(ti) - f(t) <= f(si) - f(s)
                                // i.e. f(si) - f(s) - f(ti) + f(t) >= 0
                                double violation = -w[base+si-1] - w[base+t-1];
                                if (s != all_zeros) violation += w[base+s-1];
                                if (ti != all_ones) violation += w[base+ti-1];
                                if (violation > 0) total_violation += violation;
                            }
                        }
                    }
                }
            }
        }
        //std::cout << "Num constraints: " << num_constraints <<"\n";
        //std::cout << "Min violation: " << min_violation << "\n";
        return total_violation;
    }
    virtual void Train(const std::vector<IS_PatternData*>& patterns, const std::vector<IS_LabelData*>& labels) {
        cv::Mat samples;
        std::cout << "Training submodular filters -- "; 
        std::cout.flush();
        samples.create(0, filters, CV_32FC1);
        for (const IS_PatternData* xp : patterns) {
            const cv::Mat& im = xp->m_image;
            cv::Mat response;
            GetResponse(im, response);
            samples.push_back(response);
        }
        std::cout << samples.rows << " samples...";
        std::cout.flush();
        cv::Mat best_labels;
        cv::kmeans(samples, num_clusters, best_labels, cv::TermCriteria(CV_TERMCRIT_EPS, 10, 0.2), 1, cv::KMEANS_RANDOM_CENTERS, m_centers);
        std::cout << "Done!\n";
    }
    virtual void Evaluate(const std::vector<IS_PatternData*>& patterns) override {
        std::cout << "Evaluating Contrast Submodular Features...";
        std::cout.flush();
        for (const IS_PatternData* xp : patterns) {
            const cv::Mat& im = xp->m_image;
            cv::Mat response;
            cv::Mat& features = m_patch_feature[xp->Name()];
            GetResponse(im, response);
            features.create(im.rows, im.cols, CV_32SC1);
            Classify(response, features, m_centers);
        }
        std::cout << "Done!\n";
    }
    virtual void SaveEvaluation(const std::string& output_dir) const {
        std::string outfile = output_dir + "/contrast-submodular-feature.dat";
        std::ofstream os(outfile, std::ios_base::binary);
        boost::archive::binary_oarchive ar(os);
        ar & m_patch_feature;
    }
    virtual void LoadEvaluation(const std::string& output_dir) {
        std::string infile = output_dir + "/contrast-submodular-feature.dat";
        std::ifstream is(infile, std::ios_base::binary);
        boost::archive::binary_iarchive ar(is);
        ar & m_patch_feature;
    }
    private:
    static constexpr size_t filters = 5;
    void FilterResponse(const cv::Mat& im, cv::Mat& out) {
        ASSERT(im.channels() == 1);
        ASSERT(im.type() == CV_32FC1);
        const size_t samples = im.rows*im.cols;
        out.create(samples, filters, CV_32FC1);
        std::vector<cv::Mat> all_filtered;
        cv::Mat filtered;
        filtered.create(im.rows, im.cols, CV_32FC1);
        cv::Sobel(im, filtered, CV_32F, 1, 0);
        all_filtered.push_back(filtered);
        cv::Sobel(im, filtered, CV_32F, 0, 1);
        all_filtered.push_back(filtered);
        cv::Sobel(im, filtered, CV_32F, 2, 0);
        all_filtered.push_back(filtered);
        cv::Sobel(im, filtered, CV_32F, 0, 2);
        all_filtered.push_back(filtered);
        cv::Sobel(im, filtered, CV_32F, 1, 1);
        all_filtered.push_back(filtered);
        for (int i = 0; i < 5; ++i) {
            filtered = all_filtered[i];
            cv::Point pf, po;
            po.x = i;
            po.y = 0;
            for (pf.y = 0; pf.y < im.rows; ++pf.y) {
                for (pf.x = 0; pf.x < im.cols; ++pf.x) {
                    out.at<double>(pf) = filtered.at<double>(pf);
                    po.y++;
                }
            }
        }
    }
    void GetResponse(const cv::Mat& im, cv::Mat& response) {
            cv::Mat tmp;
            im.convertTo(tmp, CV_32F);
            cv::Mat grayscale;
            cv::cvtColor(tmp, grayscale, CV_BGR2GRAY);
            grayscale *= 1./255;
            FilterResponse(grayscale, response);
    }
    void Subsample(const cv::Mat& in, cv::Mat& out, size_t num_samples) {
        std::uniform_int_distribution<size_t> dist(0, in.rows-1);
        out.create(num_samples, in.cols, in.type());
        for (size_t i = 0; i < num_samples; ++i) {
            size_t r = dist(gen);
            in.row(r).copyTo(out.row(i));
        }
    }
    void Classify(const cv::Mat& response, cv::Mat& out, const cv::Mat& centers) {
        ASSERT((size_t)response.cols == filters);
        ASSERT((size_t)centers.cols == filters);
        size_t i = 0;
        cv::Point p;
        for (p.y = 0; p.y < out.rows; ++p.y) {
            for (p.x = 0; p.x < out.cols; ++p.x) {
                cv::Mat row = response.row(i);
                double min_dist = std::numeric_limits<double>::max();
                int min_center = 0;
                for (int j = 0; j < centers.rows; ++j) {
                    cv::Mat center = centers.row(j);
                    double dist = cv::norm(row, center);
                    if (dist < min_dist) {
                        min_dist = dist;
                        min_center = j;
                    }
                }
                ASSERT(min_dist < std::numeric_limits<double>::max());
                out.at<int>(p) = min_center;
                i++;
            }
        }
    }
    friend class boost::serialization::access;
    template <class Archive>
    void serialize(Archive& ar, const unsigned int version) { 
        //std::cout << "Serializing ContrastSubmodularFeature\n";
        ar & boost::serialization::base_object<FeatureGroup>(*this);
        ar & m_scale;
    }
    mutable std::map<std::string, cv::Mat> m_patch_feature;
    std::mt19937 gen;
    cv::Mat m_centers;
};

BOOST_CLASS_EXPORT_GUID(ContrastSubmodularFeature, "ContrastSubmodularFeature")

#endif
