/*
 * File copied from the github repsoitory on 5th April 2017 :
 * https://github.com/sat-p/FMT-OMPL-EE698G
 * 
 * This implementaion of Fast Marching Tree* has been implemented as part
 * of a course project for EE698G : Probabilistic Mobile Robotics,
 * Indian Institure of Technology, Kanpur
 * 
 */

#include <ompl/geometric/planners/fmt/FMTclone.h>

#include <cmath>
#include <stack>

/************************************************************************/

typedef ompl::geometric::EE698G::FMTclone FMTClass;

/************************************************************************/

static constexpr double pi = 3.14159265359;
static constexpr double maxDouble = std::numeric_limits<double>::max();

/************************************************************************/

FMTClass::FMTclone (const base::SpaceInformationPtr &si) :
    ompl::base::Planner (si, "PMR EE698G")
{
    numSamples_ = 10000;
    
    OMPL_DEBUG ("%s : Constructor of FMTclone called", getName().c_str());
    std::cerr << "Constructor of FMTclone called" << std::endl;
    /*
     * Declaring parameters.
     */
    typedef ompl::base::Planner Planner;
    
    Planner::declareParam<unsigned> ("numSamples", this,
                                     &FMTclone::setNumSamples,
                                     &FMTclone::getNumSamples,
                                     "1:10:1000000");
    
    Planner::declareParam<double> ("distMultiplier", this,
                                   &FMTclone::setDistMultiplier,
                                   &FMTclone::getDistMultiplier,
                                   "0.1:0.01:10.");
    
    std::cerr << "Exiting Constructor of FMTclone" << std::endl;
}

/************************************************************************/

FMTClass::~FMTclone (void)
{
    OMPL_DEBUG ("%s : Destructor of FMTclone called", getName().c_str());
    std::cerr << "Destructor of FMTclone called" << std::endl;
    clear();
    std::cerr << "Exiting destructor of FMTclone" << std::endl;
}

/************************************************************************/
/************************************************************************/

void FMTClass::setup (void)
{
    OMPL_DEBUG ("%s : setup() called", getName().c_str());
    std::cerr << "setup() called" << std::endl;
    // Setting up the sampler
    sampler_ = si_->allocStateSampler();
    
    // Checking if the problem definition has been set
    if (!pdef_) {
    
        OMPL_ERROR ("%s : setup() called without problem definition"
                    " being set. Setup canceled", getName().c_str());
        setup_ = false;
        
        return;
    }
    
    if (pdef_->hasOptimizationObjective())
        opt_ = pdef_->getOptimizationObjective();
    else {
    
        OMPL_INFORM ("%s : Problem definition lacks optimization objective."
                     " Using path length.", getName().c_str());
        
        typedef ompl::base::PathLengthOptimizationObjective PLO;
        
        opt_ = std::make_shared<PLO> (si_);
        pdef_->setOptimizationObjective (opt_);
    }
    
    V_.setDistanceFunction ([this](const ompl::base::State* x1,
                                   const ompl::base::State* x2)
                            { return opt_->motionCost(x1, x2).value(); });
    
    setup_ = true;
    std::cerr << "Exiting setup(" << std::endl;
}

/************************************************************************/

void FMTClass::clear (void)
{
    OMPL_DEBUG ("%s : clear() called", getName().c_str());
    std::cerr << "clear() called" << std::endl;
    const unsigned reserveSize = numSamples_ + 5;
        
    goal_ = nullptr;
    
    Planner::clear();
    sampler_.reset(); // Destroying sampler
    
    auxData_.clear();
    auxData_.reserve (reserveSize);
    
    while (V_open_.size())
        V_open_.pop();
    
    free();
    
    V_.clear();
    std::cerr << "Exiting clear()" << std::endl;
}

/************************************************************************/
/************************************************************************/

void FMTClass::free (void)
{
    OMPL_DEBUG ("%s : free() called", getName().c_str());
    std::cerr << "free() called" << std::endl;
    stateVector nodes;
    V_.list (nodes); // Fetching all nodes;
    
    for (auto* it : nodes)
        si_->freeState (const_cast<ompl::base::State*> (it));   
    std::cerr << "Exiting free()" << std::endl;
}

/************************************************************************/
/************************************************************************/

ompl::base::PlannerStatus
FMTClass::solve (const base::PlannerTerminationCondition &tc)
{
    OMPL_DEBUG ("%s : solve() called", getName().c_str());
    std::cerr << "solve() called" << std::endl;
    // Checking if current state is valid.
    // Calls parent class's member function checkValidity();
    // Throws exception if the planner is in an invalid state.
    checkValidity();
    
    // Checking if start states are available
    if (!pis_.haveMoreStartStates()) {
    
        OMPL_ERROR ("%s: Invalid Start", getName().c_str());
        return ompl::base::PlannerStatus::INVALID_START;
    }
    
    /*
     * Adding start states to V_ and V_open_
     */
    sampleStart();
    
    /*
     * Sampling N states from the free configuration space.
     */
    sampleFree (tc);
    
    /*
     * Ensuring that there are states near the goals
     */
    
    auto* goal = dynamic_cast<ompl::base::GoalSampleableRegion*> (
                                                pdef_->getGoal().get());
    
    // Checking if goal is valid
    if (!goal) {
        
        return ompl::base::PlannerStatus::UNRECOGNIZED_GOAL_TYPE;
    }
    
    sampleGoal (goal);
    
    /*
     * Initialization for the main loop
     */
    
    r_n_ = neighborDistance();
    
    OMPL_INFORM ("%s : The value of r_n_ is %lf", getName().c_str(), r_n_);
    
    // Choosing one of the start states.
    double cost;
    const ompl::base::State* z = nullptr;
    
    std::tie (cost, z) = V_open_.top(); 
    
    saveNear (z);
    
    stateVector nodes;
    V_.list (nodes);
    
    /*
     * The main loop
     */
    while (!tc) {
        
        std::cerr << "This size of V_open_ is " << V_open_.size() << std::endl;
        
        if (goal->isSatisfied (z)) {
            OMPL_INFORM ("%s: Found solution", getName().c_str());
            goal_ = z;
            
            addSolutionPath();
            std::cerr << "Exiting solve()" << std::endl;
            typedef ompl::base::PlannerStatus PS;
            return PS (PS::StatusType::EXACT_SOLUTION);
        }
        
        auto& zData = auxData_.at (z);
        
        for (const auto& x : zData.nbh) {
            
            auto& xData = auxData_.at (x);
            
            if (xData.setType == FMT_SetType::UNVISITED) {
            
                saveNear (x);
                
                const ompl::base::State* bestParent = nullptr;
                double bestCost = maxDouble;
                
                for (const auto& y : xData.nbh) {
            
                    const auto& yData = auxData_.at (y);
            
                    if (yData.setType == FMT_SetType::OPEN) {
                    
                        const double cost = yData.cost +
                                            opt_->motionCost (x, y).value();
                        if (cost < bestCost) {
                        
                            bestCost = cost;
                            bestParent = y;
                        }
                    }
                }
                
                if (bestParent && si_->checkMotion (x, bestParent)) {
                    
                    xData.setType = FMT_SetType::OPEN;
                    xData.cost = bestCost;
                    xData.parent = bestParent;
                    
                    V_open_.emplace (bestCost, x);
                }
            }
        }
        
        zData.setType = FMT_SetType::CLOSED;
        V_open_.pop();
        
        if (V_open_.empty())
            break;
        
        std::tie (cost, z) = V_open_.top();
    }
    
    typedef ompl::base::PlannerStatus PS;
    std::cerr << "Exiting solve()" << std::endl;
    
    return PS (PS::StatusType::TIMEOUT);
}

/************************************************************************/
/************************************************************************/

void FMTClass::getPlannerData (ompl::base::PlannerData &data) const
{
    std::cerr << "Called getPlannerData()" << std::endl;
    OMPL_DEBUG ("%s : getPlannerData() called", getName().c_str());
    
    typedef ompl::base::PlannerDataVertex data_t;
    
    Planner::getPlannerData (data);
    
//     if (goal_)
//         data.addGoalVertex (data_t (goal_));
    
    for (const auto& aux : auxData_) {
        
        data.addVertex (aux.first);
        
        if (aux.second.setType == FMT_SetType::UNVISITED)
            continue;
        
        std::cerr << "adding vertex" << std::endl;

        
//         if (aux.second.parent)
//             data.addEdge (data_t (aux.second.parent), data_t (aux.first));
//         else
//             data.addStartVertex (data_t (aux.first));
    }
    std::cerr << "Exiting getPlannerData()" << std::endl;
}

/************************************************************************/

void FMTClass::addSolutionPath (void)
{
    std::cerr << "Called addSolutionPath()" << std::endl;
    OMPL_DEBUG ("%s : addSolutionPath() called", getName().c_str());
    
    if (goal_ == nullptr)
        return;
    
    std::stack<const ompl::base::State*> stack;
    
    auto path (std::make_shared<ompl::geometric::PathGeometric> (si_));
    
    const ompl::base::State* node = goal_;
    
    while (node) {
    
        stack.push (node);
        node = auxData_.at(node).parent;
    }
    
    while (stack.size()) {
    
        path->append (stack.top());
        stack.pop();
    }
    
    pdef_->addSolutionPath (path);
    std::cerr << "Exiting addSolutionPath()" << std::endl;
}

/************************************************************************/
/************************************************************************/

void FMTClass::sampleStart (void)
{
    OMPL_DEBUG ("%s : sampleStart() called", getName().c_str());
    
    while (const ompl::base::State* start = pis_.nextStart()) {
        
        ompl::base::State* node = si_->allocState();
        si_->copyState (node, start);
        
        V_.add (node);
        V_open_.emplace (0, node);
        auxData_.emplace (std::piecewise_construct,
                          std::forward_as_tuple (node),
                          std::forward_as_tuple (FMT_SetType::OPEN, 0));
    }
}

/************************************************************************/
    
void
FMTClass::sampleFree (const ompl::base::PlannerTerminationCondition &tc)
{
    OMPL_DEBUG ("%s : sampleFree() called", getName().c_str());
    
    unsigned attempts = 0;
    unsigned numSampled = 0;
    
    ompl::base::State* sample = si_->allocState();
    
    // Keep on sampling until the required number of samples haven't
    // been sampled and the termination condition has not been met.
    while (numSampled < numSamples_ && !tc) {
        ++attempts;
        
        sampler_->sampleUniform (sample);
        
        // Checking if the sampled point is valid
        if (si_->isValid (sample)) {
        
            ++numSampled;
            
            V_.add (sample);
            auxData_.emplace (sample, FMT_SetType::UNVISITED);
            
            sample = si_->allocState();
        }
    }
    
    si_->freeState (sample);
    
    // Setting the best estimate of the free Volume.
    mu_free_ = freeVolume (attempts, numSampled);
}

/************************************************************************/

void FMTClass::sampleGoal (const ompl::base::GoalSampleableRegion* goal)
{
    OMPL_DEBUG ("%s : sampleGoal() called", getName().c_str());
    
    const auto threshold = goal->getThreshold();
    
    // Ensuring that there a valid state near each goal.
    while (const ompl::base::State* goalState = pis_.nextGoal()) {
        
        ompl::base::State* nearest = si_->allocState();
        V_.nearest (nearest);
        
        // if nearest neighbor is further than threshold
        if (opt_->motionCost(goalState, nearest).value() > threshold) {
            
            si_->copyState (nearest, goalState);
            V_.add (nearest);
            auxData_.emplace (nearest, FMT_SetType::UNVISITED);
        }
        else
            si_->freeState (nearest);
    }
}

/************************************************************************/
/************************************************************************/

void FMTClass::saveNear (const ompl::base::State* z)
{
    OMPL_DEBUG ("%s : saveNear() called", getName().c_str());
    
    auto& zData = auxData_.at (z);
    
    if (zData.nnSearched)
        return;
    else {
        
        zData.nnSearched = true;
        V_.nearestR (z, r_n_, zData.nbh);
    }
}

/************************************************************************/
/************************************************************************/

double FMTClass::unitBallVolume (const unsigned dim) const
{
    OMPL_DEBUG ("%s : unitBallVolume() called", getName().c_str());
    
    if (!dim)
        return 1.0;
    else if (dim == 1)
        return 2.0;
    else
        return 2 * pi * unitBallVolume (dim - 2) / dim;
}

/************************************************************************/

double FMTClass::freeVolume
(const unsigned attempts, const unsigned samples) const
{
    OMPL_DEBUG ("%s : freeVolume() called", getName().c_str());
    
    return  (si_->getSpaceMeasure() / attempts) * samples;
}

/************************************************************************/

double FMTClass::neighborDistance (void) const
{
    OMPL_DEBUG ("%s : neighborDistance() called", getName().c_str());
    
    const double d = si_->getStateDimension();
    const double d_inv = 1 / d;
    
    const double ballVolume = unitBallVolume (d);
    
    const double gamma = 2 * std::pow (d_inv * mu_free_ / ballVolume,
                                       d_inv);
    
    const unsigned& n = numSamples_;
    
    return distMultiplier_ * gamma *
           std::pow (std::log (static_cast <double> (n)) / n,
                     d_inv);
}

/************************************************************************/
/************************************************************************/