/***********************************************************************/
/*                                                                     */
/*   svm_struct_api.c                                                  */
/*                                                                     */
/*   Definition of API for attaching implementing SVM learning of      */
/*   structures (e.g. parsing, multi-label classification, HMM)        */ 
/*                                                                     */
/*   Author: Thorsten Joachims                                         */
/*   Date: 03.07.04                                                    */
/*                                                                     */
/*   Copyright (c) 2004  Thorsten Joachims - All rights reserved       */
/*                                                                     */
/*   This software is available for non-commercial use only. It must   */
/*   not be modified and distributed without prior permission of the   */
/*   author. The author is not responsible for implications from the   */
/*   use of this software.                                             */
/*                                                                     */
/***********************************************************************/

extern "C" {
#include "svm_struct/svm_struct_common.h"
#include "svm_struct_api.h"
}
#include "svm_c++.hpp"
#include "feature.hpp"
#include <fstream>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include "serialize_unique_ptr.hpp"

static PATTERN MakePattern(PatternData* d) {
    PATTERN p;
    p.data = d;
    return p;
}

static LABEL MakeLabel(LabelData* d) {
    LABEL l;
    l.data = d;
    return l;
}

void        svm_struct_learn_api_init(int argc, char* argv[])
{
  /* Called in learning part before anything else is done to allow
     any initializations that might be necessary. */
    g_application = SVM_Cpp_Base::newUserApplication();
}

void        svm_struct_learn_api_exit()
{
  /* Called in learning part at the very end to allow any clean-up
     that might be necessary. */
}

void        svm_struct_classify_api_init(int argc, char* argv[])
{
  /* Called in prediction part before anything else is done to allow
     any initializations that might be necessary. */
    g_application = SVM_Cpp_Base::newUserApplication();
}

void        svm_struct_classify_api_exit()
{
  /* Called in prediction part at the very end to allow any clean-up
     that might be necessary. */
}

SAMPLE      read_struct_examples(char *file, STRUCT_LEARN_PARM *sparm)
{
    /* Reads struct examples and returns them in sample. The number of
       examples must be written into sample.n */
    SAMPLE   sample;    /* sample */
    EXAMPLE  *examples;

    g_application->m_testStats.ResetTimer();

    SVM_Cpp_Base::PatternVec patterns;
    SVM_Cpp_Base::LabelVec labels;

    strcpy(sparm->data_file, file);

    g_application->readExamples(file, patterns, labels);
    auto n = patterns.size();

    examples=static_cast<EXAMPLE *>(my_malloc(sizeof(EXAMPLE)*n));
    for (size_t i = 0; i < n; ++i) {
        examples[i].x = MakePattern(patterns[i].release());
        examples[i].y = MakeLabel(labels[i].release());
    }

    std::cout << " (" << n << " examples)... ";

    g_application->m_testStats.m_num_examples = n;

    sample.n=n;
    sample.examples=examples;
    return(sample);
}

void        init_struct_model(SAMPLE sample, STRUCTMODEL *sm, 
			      STRUCT_LEARN_PARM *sparm, LEARN_PARM *lparm, 
			      KERNEL_PARM *kparm)
{
    /* Initialize structmodel sm. The weight vector w does not need to be
       initialized, but you need to provide the maximum size of the
       feature space in sizePsi. This is the maximum number of different
       weights that can be learned. Later, the weight vector w will
       contain the learned weights for the model. */
      g_application->initFeatures();
      sm->sizePsi = g_application->numFeatures(); 
}

CONSTSET    init_struct_constraints(SAMPLE sample, STRUCTMODEL *sm, 
				    STRUCT_LEARN_PARM *sparm)
{
    /* Initializes the optimization problem. Typically, you do not need
       to change this function, since you want to start with an empty
       set of constraints. However, if for example you have constraints
       that certain weights need to be positive, you might put that in
       here. The constraints are represented as lhs[i]*w >= rhs[i]. lhs
       is an array of feature vectors, rhs is an array of doubles. m is
       the number of constraints. The function returns the initial
       set of constraints. */
    CONSTSET c;

    typedef FeatureGroup::Constr Constr;
    Constr constrs;
    size_t feature_base = 1; 
    for (const auto& fgp : g_application->features()) {
        Constr new_constrs = fgp->CollectConstrs(feature_base, g_application->constraintScale());
        constrs.insert(constrs.end(), new_constrs.begin(), new_constrs.end());
        feature_base += fgp->NumFeatures();
    }
    c.m = constrs.size();
    if (c.m == 0) {
        c.lhs = nullptr;
        c.rhs = nullptr;
        return c;
    }
    c.lhs = static_cast<DOC**>(my_malloc(sizeof(DOC*)*(constrs.size())));
    c.rhs = static_cast<double*>(my_malloc(sizeof(double)*(constrs.size())));
    size_t i = 0;
    for (auto constr : constrs) {
        auto lhs = constr.first;
        auto rhs = constr.second;
        std::vector<WORD> words;
        WORD w;
        for (auto p : lhs) {
            w.wnum = p.first;
            w.weight = p.second;
            words.push_back(w);
        }
        w.wnum = 0;
        words.push_back(w);
        c.lhs[i] = create_example(i, 0, sample.n+2+i, 1.0, create_svector(words.data(), NULL, 1.0));
        c.rhs[i] = rhs;
        i++;
    }
    return c;
}

LABEL       classify_struct_example(PATTERN x, STRUCTMODEL *sm, 
				    STRUCT_LEARN_PARM *sparm)
{
    /* Finds the label yhat for pattern x that scores the highest
       according to the linear evaluation function in sm, especially the
       weights sm.w. The returned label is taken as the prediction of sm
       for the pattern x. The weights correspond to the features defined
       by psi() and range from index 1 to index sm->sizePsi. If the
       function cannot find a label, it shall return an empty label as
       recognized by the function empty_label(y). */
    LABEL y;

    g_application->m_testStats.ResetTimer();

    y.data = g_application->classify(*x.data, sm->w).release();

    g_application->m_testStats.StopTimer();

    return y;
}

LABEL       find_most_violated_constraint_slackrescaling(PATTERN x, LABEL y, 
						     STRUCTMODEL *sm, 
						     STRUCT_LEARN_PARM *sparm)
{
  /* Finds the label ybar for pattern x that that is responsible for
     the most violated constraint for the slack rescaling
     formulation. For linear slack variables, this is that label ybar
     that maximizes

            argmax_{ybar} loss(y,ybar)*(1-psi(x,y)+psi(x,ybar)) 

     Note that ybar may be equal to y (i.e. the max is 0), which is
     different from the algorithms described in
     [Tschantaridis/05]. Note that this argmax has to take into
     account the scoring function in sm, especially the weights sm.w,
     as well as the loss function, and whether linear or quadratic
     slacks are used. The weights in sm.w correspond to the features
     defined by psi() and range from index 1 to index
     sm->sizePsi. Most simple is the case of the zero/one loss
     function. For the zero/one loss, this function should return the
     highest scoring label ybar (which may be equal to the correct
     label y), or the second highest scoring label ybar, if
     Psi(x,ybar)>Psi(x,y)-1. If the function cannot find a label, it
     shall return an empty label as recognized by the function
     empty_label(y). */
    LABEL ybar;

    /* insert your code for computing the label ybar here */

    assert(false /* Unimplemented! */);

    return(ybar);
}

LABEL       find_most_violated_constraint_marginrescaling(PATTERN x, LABEL y, 
						     STRUCTMODEL *sm, 
						     STRUCT_LEARN_PARM *sparm)
{
  /* Finds the label ybar for pattern x that that is responsible for
     the most violated constraint for the margin rescaling
     formulation. For linear slack variables, this is that label ybar
     that maximizes

            argmax_{ybar} loss(y,ybar)+psi(x,ybar)

     Note that ybar may be equal to y (i.e. the max is 0), which is
     different from the algorithms described in
     [Tschantaridis/05]. Note that this argmax has to take into
     account the scoring function in sm, especially the weights sm.w,
     as well as the loss function, and whether linear or quadratic
     slacks are used. The weights in sm.w correspond to the features
     defined by psi() and range from index 1 to index
     sm->sizePsi. Most simple is the case of the zero/one loss
     function. For the zero/one loss, this function should return the
     highest scoring label ybar (which may be equal to the correct
     label y), or the second highest scoring label ybar, if
     Psi(x,ybar)>Psi(x,y)-1. If the function cannot find a label, it
     shall return an empty label as recognized by the function
     empty_label(y). */
    LABEL ybar;

    g_application->m_testStats.m_num_inferences++;
    /* insert your code for computing the label ybar here */
    ybar.data = g_application->findMostViolatedConstraint(*x.data, *y.data, sm->w).release();

    return ybar;
}

int         empty_label(LABEL y)
{
  /* Returns true, if y is an empty label. An empty label might be
     returned by find_most_violated_constraint_???(x, y, sm) if there
     is no incorrect label that can be found for x, or if it is unable
     to label x at all */
  return 0;
}

SVECTOR     *psi(PATTERN x, LABEL y, STRUCTMODEL *sm,
		 STRUCT_LEARN_PARM *sparm)
{
  /* Returns a feature vector describing the match between pattern x
     and label y. The feature vector is returned as a list of
     SVECTOR's. Each SVECTOR is in a sparse representation of pairs
     <featurenumber:featurevalue>, where the last pair has
     featurenumber 0 as a terminator. Featurenumbers start with 1 and
     end with sizePsi. Featuresnumbers that are not specified default
     to value 0. As mentioned before, psi() actually returns a list of
     SVECTOR's. Each SVECTOR has a field 'factor' and 'next'. 'next'
     specifies the next element in the list, terminated by a NULL
     pointer. The list can be though of as a linear combination of
     vectors, where each vector is weighted by its 'factor'. This
     linear combination of feature vectors is multiplied with the
     learned (kernelized) weight vector to score label y for pattern
     x. Without kernels, there will be one weight in sm.w for each
     feature. Note that psi has to match
     find_most_violated_constraint_???(x, y, sm) and vice versa. In
     particular, find_most_violated_constraint_???(x, y, sm) finds
     that ybar!=y that maximizes psi(x,ybar,sm)*sm.w (where * is the
     inner vector product) and the appropriate function of the
     loss + margin/slack rescaling method. See that paper for details. */

    std::vector<WORD> words;
    FNUM fnum = 1;
    WORD w;
    for (const auto& fgp : g_application->features()) {
        std::vector<FVAL> values = fgp->Psi(*x.data, *y.data);
        assert(values.size() == fgp->NumFeatures());
        for (FVAL v : values) {
            w.wnum = fnum++;
            w.weight = v;
            words.push_back(w);
        }
    }
    w.wnum = 0;
    words.push_back(w);

    return create_svector(words.data(), nullptr, 1.0);
}

double      loss(LABEL y, LABEL ybar, STRUCT_LEARN_PARM *sparm)
{
    /* Put your code for different loss functions here. But then
       find_most_violated_constraint_???(x, y, sm) has to return the
       highest scoring label with the largest loss. */
    return g_application->loss(*y.data, *ybar.data)*g_application->lossScale();
}

int         finalize_iteration(double ceps, int cached_constraint,
			       SAMPLE sample, STRUCTMODEL *sm,
			       CONSTSET cset, double *alpha, 
			       STRUCT_LEARN_PARM *sparm)
{
  /* This function is called just before the end of each cutting plane iteration. ceps is the amount by which the most violated constraint found in the current iteration was violated. cached_constraint is true if the added constraint was constructed from the cache. If the return value is FALSE, then the algorithm is allowed to terminate. If it is TRUE, the algorithm will keep iterating even if the desired precision sparm->epsilon is already reached. */
    g_application->m_testStats.m_train_iters++;
    return g_application->finalizeIteration();
}

void        final_train_stats(double maxdiff, double epsilon, 
                   double modellength, double slacksum) {
    g_application->m_testStats.StopTimer();
    g_application->m_testStats.m_train_time = g_application->m_testStats.LastTime().count();
    g_application->m_testStats.m_maxdiff = maxdiff;
    g_application->m_testStats.m_epsilon = epsilon;
    g_application->m_testStats.m_modellength = modellength;
    g_application->m_testStats.m_slacksum = slacksum;
}

void        print_struct_learning_stats(SAMPLE sample, STRUCTMODEL *sm,
					CONSTSET cset, double *alpha, 
					STRUCT_LEARN_PARM *sparm)
{
    /* This function is called after making all test predictions in
     svm_struct_classify and allows computing and printing any kind of
     evaluation (e.g. precision/recall) you might want. You can use
     the function eval_prediction to accumulate the necessary
     statistics for each prediction. */
    if (g_application->m_statsFile != std::string())
        g_application->m_testStats.Write(g_application->m_statsFile);
}

void        print_struct_testing_stats(SAMPLE sample, STRUCTMODEL *sm,
				       STRUCT_LEARN_PARM *sparm, 
				       STRUCT_TEST_STATS *teststats)
{
    //g_application->print_struct_testing_stats(sample, sm, sparm, teststats);
}

void        eval_prediction(long exnum, EXAMPLE ex, LABEL ypred, 
			    STRUCTMODEL *sm, STRUCT_LEARN_PARM *sparm, 
			    STRUCT_TEST_STATS *teststats)
{
    //g_application->eval_prediction(exnum, ex, ypred, sm, sparm, teststats);
    /* This function allows you to accumlate statistic for how well the
       predicition matches the labeled example. It is called from
       svm_struct_classify. See also the function
       print_struct_testing_stats. */
    if(exnum == 0) { /* this is the first time the function is
              called. So initialize the teststats */
    }

    double loss = g_application->loss(*ex.y.data, *ypred.data)*100.0;
    g_application->m_testStats.Add(TestStats::ImageStats{loss, g_application->m_testStats.LastTime()});

    g_application->evalPrediction(*ex.x.data, *ex.y.data, *ypred.data);
}

void        write_struct_model(char *file, STRUCTMODEL *sm, 
			       STRUCT_LEARN_PARM *sparm)
{
  /* Writes structural model sm to file file. */
    MODEL *model = sm->svm_model;
    SVECTOR *v;
    long j,i,sv_num;

    g_application->m_testStats.m_model_file = std::string(file);

    std::ofstream ofs(file, std::ios_base::trunc | std::ios_base::out);
    boost::archive::text_oarchive ar(ofs);

    std::string version = std::string(INST_VERSION);
    ar & version;
    ar & sparm->loss_function;
    ar & model->kernel_parm.kernel_type;
    ar & model->kernel_parm.poly_degree;
    ar & model->kernel_parm.rbf_gamma;
    ar & model->kernel_parm.coef_lin;
    ar & model->kernel_parm.coef_const;
    ar & model->kernel_parm.custom;
    ar & model->totwords;
    ar & model->totdoc;

    ar & g_application->m_testStats;
    g_application->saveState(ar);
//    unsigned int param_version = m_derived->Params().Version();
//    ar & param_version;
//    m_derived->SerializeParams(ar, param_version);
//    ar & m_testStats;

    sv_num=1;
    for(i=1;i<model->sv_num;i++) {
    for(v=model->supvec[i]->fvec;v;v=v->next) 
      sv_num++;
    }
    ar & sv_num;
    ar & model->b;

    for(i=1;i<model->sv_num;i++) {
    for(v=model->supvec[i]->fvec;v;v=v->next) {
        double factor = (model->alpha[i]*v->factor);
        ar & factor;
        ar & v->kernel_id;
        size_t num_words = 0;
        for (j=0; (v->words[j]).wnum; j++) num_words++;
        ar & num_words;
        for (j=0; (v->words[j]).wnum; j++) {
            ar & (v->words[j]).wnum;
            ar & (v->words[j]).weight;
        }
        assert(!v->userdefined);
    /* NOTE: this could be made more efficient by summing the
       alpha's of identical vectors before writing them to the
       file. */
    }
    }
    ofs.close();
}

STRUCTMODEL read_struct_model(char *file, STRUCT_LEARN_PARM *sparm)
{
  /* Reads structural model sm from file file. This function is used
     only in the prediction module, not in the learning module. */
    STRUCTMODEL sm;
    std::ifstream ifs(file);
    boost::archive::text_iarchive ar(ifs);
    
    strcpy(sparm->model_file, file);

    sm.svm_model = static_cast<MODEL*>(my_malloc(sizeof(MODEL)));
    MODEL* model = sm.svm_model;

    std::string inst_version;
    ar & inst_version;
    assert(inst_version == std::string(INST_VERSION));
    ar & sparm->loss_function;
    ar & model->kernel_parm.kernel_type;
    ar & model->kernel_parm.poly_degree;
    ar & model->kernel_parm.rbf_gamma;
    ar & model->kernel_parm.coef_lin;
    ar & model->kernel_parm.coef_const;
    ar & model->kernel_parm.custom;
    ar & model->totwords;
    ar & model->totdoc;

    ar & g_application->m_testStats;
    g_application->loadState(ar);
    g_application->initFeatures();
//    ar & m_testStats;

    ar & model->sv_num;
    ar & model->b;

    model->supvec = static_cast<DOC**>(my_malloc(sizeof(DOC *)*model->sv_num));
    model->alpha = static_cast<double*>(my_malloc(sizeof(double)*model->sv_num));
    model->index=NULL;
    model->lin_weights=NULL;


    for(int i = 1; i < model->sv_num; i++) {
        long kernel_id;
        size_t num_words;
        ar & model->alpha[i];
        ar & kernel_id;
        ar & num_words;
        std::vector<WORD> words;
        WORD w;
        for (size_t j = 0; j < num_words; j++) {
            ar & w.wnum;
            ar & w.weight;
            words.push_back(w);
        }
        w.wnum = 0;
        words.push_back(w);
        model->supvec[i] = create_example(-1, 0, 0, 0.0, create_svector(words.data(), nullptr, 1.0));
        model->supvec[i]->fvec->kernel_id = kernel_id;
    }
    ifs.close();
    return sm;
}

void        write_label(FILE* fp, LABEL y)
{
} 

void free_pattern(PATTERN x) {
    SVM_Cpp_Base::PatternDeleter d{};
    d(x.data);
}

void free_label(LABEL y) {
    SVM_Cpp_Base::LabelDeleter d{};
    d(y.data);
}

void        free_struct_model(STRUCTMODEL sm) 
{
  /* Frees the memory of model. */
  /* if(sm.w) free(sm.w); */ /* this is free'd in free_model */
  if(sm.svm_model) free_model(sm.svm_model,1);
  /* add free calls for user defined data here */
}

void        free_struct_sample(SAMPLE s)
{
  /* Frees the memory of sample s. */
  int i;
  for(i=0;i<s.n;i++) { 
    free_pattern(s.examples[i].x);
    free_label(s.examples[i].y);
  }
  free(s.examples);
}

void        print_struct_help()
{
  /* Prints a help text that is appended to the common help text of
     svm_struct_learn. */
    g_application->printLearnHelp();
}

void         parse_struct_parameters(STRUCT_LEARN_PARM *sparm)
{
  /* Parses the command line parameters that start with -- */
    g_application->parseBaseLearnParams(sparm->custom_argc, sparm->custom_argv);
}

void        print_struct_help_classify()
{
  /* Prints a help text that is appended to the common help text of
     svm_struct_classify. */
    g_application->printClassifyHelp();
}

void         parse_struct_parameters_classify(STRUCT_LEARN_PARM *sparm)
{
  /* Parses the command line parameters that start with -- for the
     classification module */
    g_application->parseBaseClassifyParams(sparm->custom_argc, sparm->custom_argv);
}

