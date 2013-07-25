// stl headers
#include <vector>
#include <stdexcept>
#include <cassert>
#include <algorithm>
#include <iterator>


// external headers
#include <lemon/maps.h>
#include <armadillo>
#include <mlpack/core.hpp>
#include <mlpack/methods/kmeans/kmeans.hpp>
#include <mlpack/methods/gmm/gmm.hpp>
#include <omp.h>


// pgmlink headers
#include "pgmlink/merger_resolving.h"
#include "pgmlink/hypotheses.h"
#include "pgmlink/event.h"
#include "pgmlink/traxels.h"


namespace pgmlink {
  ////
  //// ClusteringMlpackBase
  ////
  void ClusteringMlpackBase::copy_centers_to_feature_array(const arma::mat& centers, feature_array& c) {
    int n = centers.n_cols;
    int stepSize = centers.n_rows;
    
    if (stepSize*n != (int)c.size()) {
      throw std::range_error("Source matrix dimensions and vector dimension do not agree!");
    }

    feature_array::iterator it = c.begin();
    for (int i = 0; i < n; ++i, it += stepSize) {
      arma::vec col = centers.col(i);
      std::copy(col.begin(), col.end(), it);
    }
  }

  ////
  //// KMeans
  ////
  feature_array KMeans::operator()() {
    mlpack::kmeans::KMeans<> kMeans;
    int n = data_.size()/3;
    arma::mat data(3, n);
    arma::mat centers(3, k_);
    arma::Col<size_t> labels;
    feature_array_to_arma_mat(data_, data);
    kMeans.Cluster(data, k_, labels);
    get_centers(data, labels, centers, k_);
    feature_array fa_centers(3*k_);
    copy_centers_to_feature_array(centers, fa_centers);
    return fa_centers;
  }


  ////
  //// GMM
  ////
  feature_array GMM::operator()() {
    mlpack::gmm::GMM<> gmm(k_, n_);
    int n_samples = data_.size()/3;
    arma::mat data(n_,n_samples);
    arma::Col<size_t> labels;
    LOG(logDEBUG1) << "GMM::operator(): n_=" << n_;
    if (n_ == 2) {
      feature_array_to_arma_mat_skip_last_dimension(data_, data, 3);
    } else if(n_ == 3) {
      feature_array_to_arma_mat(data_, data);
    } else {
      throw std::runtime_error("Number of spatial dimensions other than 2 or 3 would not make sense!");
    }
    score_ = gmm.Estimate(data, n_trials_);
    std::vector<arma::vec> centers = gmm.Means();
    feature_array fa_centers;
    for (std::vector<arma::vec>::iterator it = centers.begin(); it != centers.end(); ++it) {
      std::copy(it->begin(), it->end(), std::back_insert_iterator<feature_array >(fa_centers));
      if (n_ == 2) {
        fa_centers.push_back(0);
      }
    }
    return fa_centers;
  }


  /**
   * returns n*log_likelihood of the model
   */
  double GMM::score() const {
    return score_;
  }
  ////
  //// GMMInitalizeArma
  ////

  feature_array GMMInitializeArma::operator()() {
    feature_array ret;
    // std::back_insert_iterator<feature_array > bii(ret);
    // arma::vec d = this->operator()("this is so dirty!");
    // std::copy(d.begin(), d.end(), bii);
    return ret;
  }
  

  std::vector<arma::vec> GMMInitializeArma::operator()(const char*) {
    mlpack::gmm::GMM<> gmm(k_, data_.n_rows);
    score_ = gmm.Estimate(data_, n_trials_);
    std::vector<arma::vec> centers = gmm.Means();
    return centers;
  }

  
  double GMMInitializeArma::score() const {
    return score_;
  }





  /*void KMeans::copy_centers_to_feature_array(const arma::mat& centers, feature_array& c) {
    int n = centers.n_cols;
    int stepSize = centers.n_rows;
    
    if (stepSize*n != (int)c.size()) {
      throw std::range_error("Source matrix dimensions and vector dimension do not agree!");
    }

    feature_array::iterator it = c.begin();
    for (int i = 0; i < n; ++i, it += stepSize) {
      arma::vec col = centers.col(i);
      std::copy(col.begin(), col.end(), it);
    }
  }*/
 


  ////
  //// FeatureExtractorMCOMsFromKMeans
  ////
  std::vector<Traxel> FeatureExtractorMCOMsFromKMeans::operator()(Traxel trax,
                                                                  size_t nMergers,
                                                                  unsigned int max_id
                                                                  ) {
    std::map<std::string, feature_array>::iterator it = trax.features.find("coordinates");
    assert(it != trax.features.end());
    std::vector<Traxel> res;
    KMeans kmeans(nMergers, it->second);
    trax.features["mergerCOMs"] = kmeans();
    FeatureExtractorMCOMsFromMCOMs extractor;
    return extractor(trax, nMergers, max_id);
  }


  ////
  //// FeatureExtractorMCOMsFromGMM
  ////
  std::vector<Traxel> FeatureExtractorMCOMsFromGMM::operator() (Traxel trax,
                                                                size_t nMergers,
                                                                unsigned int max_id
                                                                ){
    std::map<std::string, feature_array>::iterator it = trax.features.find("coordinates");
    assert(it != trax.features.end());
    std::vector<Traxel> res;
    GMM gmm(nMergers, n_dim_, it->second);
    trax.features["mergerCOMs"] = gmm();
    FeatureExtractorMCOMsFromMCOMs extractor;
    return extractor(trax, nMergers, max_id);
  }
    


  ////
  //// FeatureExtractorMCOMsFromPCOMs
  ////
  std::vector<Traxel> FeatureExtractorMCOMsFromPCOMs::operator()(
								 Traxel trax,
								 size_t nMerger,
								 unsigned int start_id
								 ) {
    std::map<std::string, feature_array>::iterator it = trax.features.find("possibleCOMs");
    assert(it != trax.features.end());
    std::vector<Traxel> res;
    unsigned int index1 = 3*(nMerger*(nMerger-1))/2;
    unsigned int index2 = 3*(nMerger*(nMerger+1))/2;
    feature_array range(it->second.begin()+index1, it->second.begin()+index2);
    for (unsigned int n = 0; n < nMerger; ++n, ++start_id) {
      trax.Id = start_id;
      trax.features["com"] = feature_array(range.begin()+(3*n), range.begin()+(3*(n+1)));
      res.push_back(trax);
      LOG(logDEBUG3) << "FeatureExtractorMCOMsFromPCOMs::operator()(): Appended traxel with com (" << trax.features["com"][0] << "," << trax.features["com"][1] << "," << trax.features["com"][2] << ") and id " << trax.Id;
    }
    return res;
  }


  ////
  //// FeatureExtractorMCOMsFromMCOMs
  ////
  std::vector<Traxel> FeatureExtractorMCOMsFromMCOMs::operator()(
                                                                 Traxel trax,
                                                                 size_t nMerger,
                                                                 unsigned int start_id
                                                                 ) {
    std::map<std::string, feature_array>::iterator it = trax.features.find("mergerCOMs");
    assert(it != trax.features.end());
    std::vector<Traxel> res;
    for (unsigned int n = 0; n < nMerger; ++n, ++start_id) {
      trax.Id = start_id;
      trax.features["com"] = feature_array(it->second.begin()+(3*n), it->second.begin()+(3*(n+1)));
      res.push_back(trax);
    }
    return res;
  }


  ////
  //// FeatureHandlerBase
  ////
  void FeatureHandlerBase::add_arcs_for_replacement_node(HypothesesGraph& g,
                                                         HypothesesGraph::Node n,
                                                         const std::vector<HypothesesGraph::base_graph::Arc>& sources,
                                                         const std::vector<HypothesesGraph::base_graph::Arc>& targets,
                                                         DistanceBase& distance) {
    // add Arcs for new node that is a replacement node for merger node
    // get the property_maps needed for adding Arcs
    std::vector<HypothesesGraph::base_graph::Arc>::const_iterator it;
    property_map<arc_distance, HypothesesGraph::base_graph>::type& arc_distances = g.get(arc_distance());
    property_map<arc_active, HypothesesGraph::base_graph>::type& arc_active_map = g.get(arc_active());
    property_map<arc_resolution_candidate, HypothesesGraph::base_graph>::type& arc_resolution_map = g.get(arc_resolution_candidate());

    property_map<node_traxel, HypothesesGraph::base_graph>::type& traxel_map = g.get(node_traxel());

    std::vector<int> arc_ids;
    // add incoming arcs
    for (it = sources.begin(); it != sources.end(); ++it) {
      HypothesesGraph::Node from = g.source(*it);
      double dist = distance(g, from, n);
      HypothesesGraph::Arc arc = g.addArc(from, n);
      arc_ids.push_back(g.id(arc));
      arc_distances.set(arc, dist);
      arc_active_map.set(arc, true);
      arc_resolution_map.set(arc, true);
      LOG(logDEBUG4) << "FeatureHandlerBase::add_arcs_for_replacement_node: add incoming arc (" << traxel_map[g.source(arc)].Id <<
			  "," << traxel_map[g.target(arc)].Id << ") = " << arc_resolution_map[arc] << " and active = " << arc_active_map[arc];
    }

    // add outgoing arcs
    for (it = targets.begin(); it != targets.end(); ++it) {
      HypothesesGraph::Node to = g.target(*it);
      double dist = distance(g, n, to);
      HypothesesGraph::Arc arc = g.addArc(n, to);
      arc_distances.set(arc, dist);
      arc_active_map.set(arc, true);
      arc_resolution_map.set(arc, true);
      arc_ids.push_back(g.id(arc));
      LOG(logDEBUG4) << "FeatureHandlerBase::add_arcs_for_replacement_node: add outgoing arc (" << traxel_map[g.source(arc)].Id <<
    		  "," << traxel_map[g.target(arc)].Id << ") = " << arc_resolution_map[arc] << " and active = " << arc_active_map[arc];
    }
    LOG(logDEBUG) << "FeatureHandlerBase::add_arcs_for_replacement_node: checking states of arcs";
	for(std::vector<int>::const_iterator arc_it = arc_ids.begin(); arc_it != arc_ids.end(); ++arc_it) {
		HypothesesGraph::Arc arc = g.arcFromId(*arc_it);
		assert(arc_active_map[arc]);
	}
  }
    

  ////
  //// FeatureHandlerFromTraxels
  ////
  void FeatureHandlerFromTraxels::operator()(
                                             HypothesesGraph& g,
                                             HypothesesGraph::Node n,
                                             std::size_t n_merger,
                                             unsigned int max_id,
                                             int timestep,
                                             const std::vector<HypothesesGraph::base_graph::Arc>& sources,
                                             const std::vector<HypothesesGraph::base_graph::Arc>& targets,
                                             std::vector<unsigned int>& new_ids
                                             ) {
    // property maps
    property_map<node_active2, HypothesesGraph::base_graph>::type& active_map = g.get(node_active2());
    property_map<node_traxel, HypothesesGraph::base_graph>::type& traxel_map = g.get(node_traxel());
    property_map<node_timestep, HypothesesGraph::base_graph>::type& time_map = g.get(node_timestep());
    property_map<node_originated_from, HypothesesGraph::base_graph>::type& origin_map = g.get(node_originated_from());
    property_map<node_resolution_candidate, HypothesesGraph::base_graph>::type& node_resolution_map = g.get(node_resolution_candidate());

    // traxel and vector of replacement traxels
    Traxel trax = traxel_map[n];
    std::vector<Traxel> ft = extractor_(trax, n_merger, max_id);
    // MAYBE LOG
    for (std::vector<Traxel>::iterator it = ft.begin(); it != ft.end(); ++it) {
      // set traxel features, most of which can be copied from the merger node
      // set new center of mass as calculated from GMM
      // add node to graph and activate it
      HypothesesGraph::Node new_node = g.add_node(timestep);
      // MAYBE LOG
      traxel_map.set(new_node, *it);
      active_map.set(new_node, 1);
      time_map.set(new_node, timestep);

      // add arc candidates for new nodes (todo: need to somehow choose which ones are active)
      this->add_arcs_for_replacement_node(g, new_node, sources, targets, base_);
      // save new id from merger node to new_ids;
      new_ids.push_back(it->Id);
      // store parent (merger) node. this is used for creating the resolved_to event later
      origin_map.set(new_node, std::vector<unsigned int>(1, trax.Id));
      LOG(logDEBUG3) << "FeatureHandlerFromTraxels::operator(): added " << trax.Id << " to origin_map[" << g.id(new_node) << "]";
      node_resolution_map.set(new_node, true);
 
    }
  }

  
  ////
  //// DistanceFromCOMs
  ////
  double DistanceFromCOMs::operator()(const HypothesesGraph& g, HypothesesGraph::Node from, HypothesesGraph::Node to) {
    property_map<node_traxel, HypothesesGraph::base_graph>::type& traxel_map = g.get(node_traxel());
    return traxel_map[from].distance_to(traxel_map[to]);
  }

  double DistanceFromCOMs::operator()(Traxel from, Traxel to) {
    return from.distance_to(to);
  }

  ////
  //// ResolveAmbiguousArcsGreedy
  ////
  HypothesesGraph& ResolveAmbiguousArcsGreedy::operator()(HypothesesGraph* g) {
    
    return *g;
  }


  void MergerResolver::deactivate_arcs(std::vector<HypothesesGraph::base_graph::Arc> arcs) {
    // deactivate Arcs provided by arcs
    // useful to deactivate Arcs of merger Node

    property_map<arc_active, HypothesesGraph::base_graph>::type& arc_active_map = g_->get(arc_active());
    property_map<arc_resolution_candidate, HypothesesGraph::base_graph>::type& arc_resolution_map = g_->get(arc_resolution_candidate());
    for (std::vector<HypothesesGraph::base_graph::Arc>::iterator it = arcs.begin(); it != arcs.end(); ++it) {
      LOG(logDEBUG3) << "MergerResolver::deactivate_arcs(): setting arc " << g_->id(*it)  << " (" << g_->get(node_traxel())[g_->source((*it))].Id << "," << g_->get(node_traxel())[g_->target((*it))].Id << ") property arc_active to false";
      arc_active_map.set(*it, false);
      arc_resolution_map.set(*it, false);
    }
  }

  void MergerResolver::deactivate_nodes(std::vector<HypothesesGraph::Node> nodes) {
    // deactivate Nodes provided by nodes
    // needed to set all resolved merger nodes inactive
    std::vector<HypothesesGraph::Node>::iterator it = nodes.begin();
    property_map<node_active2, HypothesesGraph::base_graph>::type& node_active_map = g_->get(node_active2());
    property_map<node_resolution_candidate, HypothesesGraph::base_graph>::type& node_resolution_map = g_->get(node_resolution_candidate());
    for (; it != nodes.end(); ++it) {
      LOG(logDEBUG3) << "MergerResolver::deactivate_nodes(): setting Node " << g_->id(*it) << " property node_active2 to 0";
      node_active_map.set(*it, 0);
      node_resolution_map.set(*it, 0);
    }
  }

  unsigned int MergerResolver::get_max_id(int ts) {
    property_map<node_traxel, HypothesesGraph::base_graph>::type& traxel_map = g_->get(node_traxel());
    property_map<node_timestep, HypothesesGraph::base_graph>::type& time_map = g_->get(node_timestep());
    property_map<node_timestep, HypothesesGraph::base_graph>::type::ItemIt timeIt(time_map, ts);
    unsigned int max_id = 0;
    for (; timeIt != lemon::INVALID; ++timeIt) {
      if (traxel_map[timeIt].Id > max_id) {
    	  max_id = traxel_map[timeIt].Id;
      }
    }
    return max_id;
  }

  void MergerResolver::refine_node(HypothesesGraph::Node node,
				   std::size_t nMerger,
				   FeatureHandlerBase& handler) {
    property_map<node_timestep, HypothesesGraph::base_graph>::type& time_map = g_->get(node_timestep());
    property_map<merger_resolved_to, HypothesesGraph::base_graph>::type& resolved_map = g_->get(merger_resolved_to());
    
    int timestep = time_map[node];
    unsigned int max_id = get_max_id(timestep)+1;

    // get incoming and outgoing arcs for reorganizing arcs
    std::vector<HypothesesGraph::base_graph::Arc> sources;
    std::vector<HypothesesGraph::base_graph::Arc> targets;
    collect_arcs(HypothesesGraph::base_graph::InArcIt(*g_, node), sources);
    collect_arcs(HypothesesGraph::base_graph::OutArcIt(*g_, node), targets);

    // instead of calling everything with traxels:
    // write functor for extracting and setting features:
    // FeatureHandlerBase ft_handler_(*g, node, nMerger, max_id, sources, targets, vector<unsigned int>& new_ids);


    // create new node for each of the objects merged into node
    std::vector<unsigned int> new_ids;
    handler(*g_, node, nMerger, max_id, timestep, sources, targets, new_ids);

    // deactivate incoming and outgoing arcs of merger node
    // merger node will be deactivated after pruning
    deactivate_arcs(sources);
    deactivate_arcs(targets);
    // save information on new ids in property map
    resolved_map.set(node, new_ids);
  }


  HypothesesGraph* MergerResolver::resolve_mergers(FeatureHandlerBase& handler) {
    // extract property maps and iterators from graph
    LOG(logDEBUG) << "resolve_mergers() entered";
    property_map<node_active2, HypothesesGraph::base_graph>::type& active_map = g_->get(node_active2());
    property_map<node_active2, HypothesesGraph::base_graph>::type::ValueIt active_valueIt = active_map.beginValue();
    
    // iterate over mergers and replace merger nodes
    // keep track of merger nodes to deactivate them later
    std::vector<HypothesesGraph::Node> nodes_to_deactivate;
    for (; active_valueIt != active_map.endValue(); ++active_valueIt) {
      if (*active_valueIt > 1) {
	property_map<node_active2, HypothesesGraph::base_graph>::type::ItemIt active_itemIt(active_map, *active_valueIt);
	
	for (; active_itemIt != lemon::INVALID; ++active_itemIt) {
	  // calculate_centers<ClusteringAlg>(active_itemIt, *active_valueIt);
	  // for each object create new node and set arcs to old merger node inactive (neccessary for pruning)
	  refine_node(active_itemIt, *active_valueIt, handler);
	  nodes_to_deactivate.push_back(active_itemIt);
	}
      }
    }
    // maybe keep merger nodes active for event extraction
    deactivate_nodes(nodes_to_deactivate);

    LOG(logDEBUG) << "resolve_mergers() done";
    return g_;
  }


  void resolve_graph(HypothesesGraph& src, HypothesesGraph& dest, boost::function<double(const double)> transition, double ep_gap, bool with_tracklets, 
         const double transition_parameter, const bool with_constraints) {
	  LOG(logDEBUG) << "resolve_graph() entered";
    if (!dest.has_property(node_traxel())) {
      dest.add(node_traxel());
    }
    if (!dest.has_property(node_tracklet())) {
      dest.add(node_tracklet());
    }
    if (!dest.has_property(tracklet_intern_dist())) {
      dest.add(tracklet_intern_dist());
    }
    if (!dest.has_property(arc_distance())) {
      dest.add(arc_distance());
    }
    if (!dest.has_property(node_originated_from())) {
      dest.add(node_originated_from());
    }

    src.add(division_active()).add(arc_active()).add(node_active2());
    dest.add(division_active()).add(arc_active()).add(node_active2());
    

    std::map<HypothesesGraph::Node, HypothesesGraph::Node> nr;
    std::map<HypothesesGraph::Arc, HypothesesGraph::Arc> ar;
    std::map<HypothesesGraph::Node, HypothesesGraph::Node> ncr;
    std::map<HypothesesGraph::Arc, HypothesesGraph::Arc> acr;
    std::map<HypothesesGraph::Node, HypothesesGraph::Node> division_splits;
    std::map<HypothesesGraph::Arc, HypothesesGraph::Arc> arc_cross_reference_divisions;
    copy_hypotheses_graph_subset<node_resolution_candidate, arc_resolution_candidate>(src, dest, nr, ar, ncr, acr);
    duplicate_division_nodes(dest, division_splits, arc_cross_reference_divisions);
    std::vector<double> prob;
    prob.push_back(0.0);
    prob.push_back(1.0);
    boost::function<double(const Traxel&, const size_t)> division = NegLnDivision(1); // weight 1
    // boost::function<double(const double)> transition = NegLnTransition(1); // weight 1
    boost::function<double(const Traxel&, const size_t)> detection = boost::bind<double>(NegLnConstant(1,prob), _2);
    translate_property_value_map<node_traxel, HypothesesGraph::Node>(src, dest, nr);
    translate_property_value_map<arc_distance, HypothesesGraph::Arc>(src, dest, ar);
    translate_property_value_map<node_originated_from, HypothesesGraph::Node>(src, dest, nr);
    boost::function<double(const Traxel&)> appearance_cost = ConstantFeature(0.0);
    boost::function<double(const Traxel&)> disappearance_cost = ConstantFeature(0.0);

    LOG(logDEBUG) << "resolve_graph(): calling conservation tracking";

    ConservationTracking pgm(
                             1, //max_number_objects_,
                             detection, //detection,
                             division, // division
                             transition, // transition
                             0, // forbidden_cost_,
                             ep_gap, // ep_gap_
                             with_tracklets, // with_tracklets_
                             false, // with_divisions_
                             disappearance_cost, // disappearance_cost_
                             appearance_cost, // appearance_cost
                             false, // with_misdetections_allowed
                             false, // with appearance
                             false, // with disappearance
                             transition_parameter,
                             with_constraints
                             );

    pgm.formulate(dest);
    pgm.infer();
    pgm.conclude(dest);

    merge_split_divisions(dest, division_splits, arc_cross_reference_divisions);

    translate_property_bool_map<arc_active, HypothesesGraph::Arc>(dest, src, acr);
  }


  double calculate_BIC(int k, int n_samples, double regularization_weight, const ClusteringMlpackBase& gmm) {
    return gmm.score()/n_samples - regularization_weight*k; 
  }


  void gmm_priors_and_centers(const feature_array& data, feature_array& priors, feature_array& centers, int k_max, int ndim, double regularization_weight) {
    assert(priors.size() == 0);
    assert(centers.size() == 0);
    priors.resize(k_max);
    centers.resize((k_max*(k_max+1))/2*ndim);

    int n_samples = data.size()/ndim;
#   pragma omp parallel for
    for (int k = 1; k <= k_max; ++k) {
      GMM gmm(k, ndim, data);
      feature_array means = gmm();
      double curr_bic = calculate_BIC(k, n_samples, regularization_weight, gmm);
      priors.at(k-1) = curr_bic;
      std::copy(means.begin(), means.end(), centers.begin() + ((k-1)*k)/2*ndim);
    }
  }

  
  void gmm_priors_and_centers_arma(const arma::mat& data, feature_array& priors, feature_array& centers, int k_max, int ndim, double regularization_weight) {
    assert(priors.size() == 0);
    assert(centers.size() == 0);
    priors.resize(k_max);
    centers.resize((k_max*(k_max+1))/2*ndim);

    int n_samples = data.size()/ndim;
#   pragma omp parallel for
    for (int k = 1; k <= k_max; ++k) {
      GMMInitializeArma gmm(k, data);
      std::vector<arma::vec> means = gmm("terrible hack");
      double curr_bic = calculate_BIC(k, n_samples, regularization_weight, gmm);
      priors.at(k-1) = curr_bic;
      unsigned idx = 0;
      for (std::vector<arma::vec>::iterator it = means.begin(); it != means.end(); ++it) {
        std::copy(it->begin(), it->end(), centers.begin() + ((idx+k-1)*(idx+k))/2*ndim);
      }
    }
  }


  ////
  //// duplicate division nodes in subset graph
  ////
  void duplicate_division_nodes(HypothesesGraph& graph,
                                std::map<HypothesesGraph::Node, HypothesesGraph::Node>& division_splits,
                                std::map<HypothesesGraph::Arc, HypothesesGraph::Arc>& arc_cross_reference) {
    LOG(logDEBUG1) << "duplicate_division_nodes(): enter";
    typedef property_map<division_active, HypothesesGraph::base_graph>::type DivisionMap;
    typedef property_map<node_traxel, HypothesesGraph::base_graph>::type TraxelMap;
    typedef property_map<arc_active, HypothesesGraph::base_graph>::type ArcMap;
    typedef property_map<arc_distance, HypothesesGraph::base_graph>::type DistanceMap;
    typedef property_map<node_active2, HypothesesGraph::base_graph>::type NodeMap;
    typedef property_map<node_originated_from, HypothesesGraph::base_graph>::type OriginMap;

    DivisionMap& division_map = graph.get(division_active());
    TraxelMap& traxel_map = graph.get(node_traxel());
    ArcMap& arc_map = graph.get(arc_active());
    DistanceMap& distance_map = graph.get(arc_distance());
    NodeMap& node_map = graph.get(node_active2());
    OriginMap& origin_map = graph.get(node_originated_from());

    for (DivisionMap::TrueIt division_it(division_map);
         division_it != lemon::INVALID;
         ++division_it) {
      LOG(logDEBUG2) << "duplicate_division_nodes(): looping over division node: "
                     << "with " << lemon::countOutArcs(graph, division_it) << " outgoing arcs";
         
      if (lemon::countOutArcs(graph, division_it) < 4) {
        continue;
      }

      
      const Traxel& trax = traxel_map[division_it];
      const HypothesesGraph::Node& node = graph.add_node(trax.Timestep);
      traxel_map.set(node, trax);
      node_map.set(node, 1);
      /* for (HypothesesGraph::InArcIt arc_it(graph, node); arc_it != lemon::INVALID; ++arc_it) {
        const HypothesesGraph::Arc& arc = graph.addArc(graph.source(arc_it), node);
        arc_map.set(arc, true);
        distance_map.set(arc, distance_map[arc_it]);
        arc_cross_reference[arc] = arc_it;
        } */

      unsigned switch_node_id = 0u;
      for (HypothesesGraph::OutArcIt arc_it(graph, division_it); arc_it != lemon::INVALID; ++arc_it) {
        const HypothesesGraph::Node& target = graph.target(arc_it);
        if (origin_map[target].size() > 0) {
          LOG(logDEBUG3) << "duplicate_division_nodes(): originated from merger " << origin_map[target][0];
        }
        if (origin_map[target].size() > 0 &&
            (switch_node_id == 0 ||
             switch_node_id == origin_map[target][0])
            ) {
          LOG(logDEBUG3) << "duplicate_division_nodes(): copying outgoing arcs";
          const HypothesesGraph::Arc& arc = graph.addArc(node, target);
          arc_map.set(arc, true);
          distance_map.set(arc, distance_map[arc_it]);
          arc_cross_reference[arc] = arc_it;
          switch_node_id = origin_map[target][0];
        }
      }

      division_splits[division_it] = node;
    }
  }


  ////
  //// merge previously split divisions after inference
  ////
  void merge_split_divisions(const HypothesesGraph& graph,
                             std::map<HypothesesGraph::Node, HypothesesGraph::Node>& division_splits,
                             std::map<HypothesesGraph::Arc, HypothesesGraph::Arc>& arc_cross_reference) {
    LOG(logDEBUG1) << "merge_splut_divisions(): enter";

    typedef property_map<arc_active, HypothesesGraph::base_graph>::type ArcMap;
    typedef property_map<node_active2, HypothesesGraph::base_graph>::type NodeMap;
    typedef property_map<division_active, HypothesesGraph::base_graph>::type DivisionMap;

    DivisionMap& division_map = graph.get(division_active());
    ArcMap& arc_map = graph.get(arc_active());
    NodeMap& node_map = graph.get(node_active2());

    for (std::map<HypothesesGraph::Node, HypothesesGraph::Node>::const_iterator node_it = division_splits.begin();
         node_it != division_splits.end();
         ++node_it) {
      
      /* for (HypothesesGraph::InArcIt arc_it(graph, node_it->second); arc_it != lemon::INVALID; ++arc_it) {
        if (arc_map[arc_it]) {
          arc_map.set(arc_cross_reference[arc_it], true);
          arc_map.set(arc_it, false);
        }
        } */

      
      for (HypothesesGraph::OutArcIt arc_it(graph, node_it->second); arc_it != lemon::INVALID; ++arc_it) {
        if (arc_map[arc_it]) {
          arc_map.set(arc_cross_reference[arc_it], true);
          arc_map.set(arc_it, false);
        }
      }
      
      division_map.set(node_it->first, true);
      node_map.set(node_it->second, false);
    }
  }

}


