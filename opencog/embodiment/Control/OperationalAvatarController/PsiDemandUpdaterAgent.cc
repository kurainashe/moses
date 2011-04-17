/*
 * @file opencog/embodiment/Control/OperationalAvatarController/PsiDemandUpdaterAgent.cc
 *
 * @author Zhenhua Cai <czhedu@gmail.com>
 * @date 2011-04-01
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License v3 as
 * published by the Free Software Foundation and including the exceptions
 * at http://opencog.org/wiki/Licenses
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program; if not, write to:
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "OAC.h"
#include "PsiDemandUpdaterAgent.h"

#include "opencog/web/json_spirit/json_spirit.h"

#include<boost/tokenizer.hpp>
#include<boost/lexical_cast.hpp>

using namespace OperationalAvatarController;

bool PsiDemandUpdaterAgent::Demand::runUpdater
    (const AtomSpace & atomSpace, 
     Procedure::ProcedureInterpreter & procedureInterpreter, 
     const Procedure::ProcedureRepository & procedureRepository
    )
{
    // Get ListLink that holding the updater
    Handle hListLink = atomSpace.getOutgoing(this->hFuzzyWithin, 1);

    if ( hListLink == opencog::Handle::UNDEFINED ||
         atomSpace.getType(hListLink) != LIST_LINK || 
         atomSpace.getArity(hListLink) != 3 ) {

        logger().error("PsiDemandUpdaterAgent::Demand::%s - Expect a ListLink for demand '%s' with three arity that contains an updater (ExecutionOutputLink). But got '%s'", 
                       __FUNCTION__, 
                       this->demandName.c_str(), 
                       atomSpace.atomAsString(hListLink).c_str()
                      );
        return false; 
    }

    // Get the ExecutionOutputLink
    Handle hExecutionOutputLink = atomSpace.getOutgoing(hListLink, 2);

    if ( hExecutionOutputLink == opencog::Handle::UNDEFINED ||
         atomSpace.getType(hExecutionOutputLink) != EXECUTION_OUTPUT_LINK ||
         atomSpace.getArity(hExecutionOutputLink) != 2 ) {

        logger().error("PsiDemandUpdaterAgent::Demand::%s - Expect a ExecutionOutputLink for demand '%s' with two arity that contains an updater. But got '%s'", 
                       __FUNCTION__, 
                       this->demandName.c_str(), 
                       atomSpace.atomAsString(hExecutionOutputLink).c_str()
                      );
        return false; 
    }

    // Get the GroundedSchemaNode
    Handle hGroundedSchemaNode = atomSpace.getOutgoing(hExecutionOutputLink, 0);

    if ( hGroundedSchemaNode == opencog::Handle::UNDEFINED ||
         atomSpace.getType(hGroundedSchemaNode) != GROUNDED_SCHEMA_NODE ) {

        logger().error("PsiDemandUpdaterAgent::Demand::%s - Expect a GroundedSchemaNode for demand '%s'. But got '%s'", 
                       __FUNCTION__, 
                       this->demandName.c_str(), 
                       atomSpace.atomAsString(hGroundedSchemaNode).c_str()
                      );
        return false; 

    }

    // Run the Procedure that update Demand and get the updated value
    const std::string & updaterName = atomSpace.getName(hGroundedSchemaNode);
    std::vector <combo::vertex> schemaArguments;
    Procedure::RunningProcedureID executingSchemaId;
    combo::vertex result; // combo::vertex is actually of type boost::variant <...>

    const Procedure::GeneralProcedure & procedure =
                                        procedureRepository.get(updaterName);

    executingSchemaId = procedureInterpreter.runProcedure(procedure, schemaArguments);

    // Wait until the procedure is done
    while ( !procedureInterpreter.isFinished(executingSchemaId) )
        procedureInterpreter.run(NULL);  

    // Check if the the updater run successfully
    if ( procedureInterpreter.isFailed(executingSchemaId) ) {
        logger().error( "PsiDemandUpdaterAgent::Demand::%s - Failed to execute '%s' for updating demand '%s'", 
                         __FUNCTION__, 
                         updaterName.c_str(), 
                         this->demandName.c_str()
                      );

        return false;
    }

    // Store the updated demand value (result)
    result = procedureInterpreter.getResult(executingSchemaId);
    this->currentDemandValue = get_contin(result);

    logger().debug("PsiDemandUpdaterAgent::Demand::%s - The level of demand '%s' will be set to '%f'", 
                   __FUNCTION__, 
                   this->demandName.c_str(), 
                   this->currentDemandValue
                  );

    return true; 
}    

bool PsiDemandUpdaterAgent::Demand::updateDemandGoal
    (AtomSpace & atomSpace, 
     Procedure::ProcedureInterpreter & procedureInterpreter,
     const Procedure::ProcedureRepository & procedureRepository, 
     const unsigned long timeStamp
    )
{
    // Get the GroundedPredicateNode "FuzzyWithin"
    Handle hGroundedPredicateNode = atomSpace.getOutgoing(hFuzzyWithin, 0);

    if ( hGroundedPredicateNode == opencog::Handle::UNDEFINED ||
         atomSpace.getType(hGroundedPredicateNode) != GROUNDED_PREDICATE_NODE ) {

        logger().error("PsiDemandUpdaterAgent::Demand::%s - Expect a GroundedPredicateNode for demand '%s'. But got '%s'", 
                       __FUNCTION__, 
                       this->demandName.c_str(), 
                       atomSpace.atomAsString(hGroundedPredicateNode).c_str()
                      );
        return false; 
    }

    // Get ListLink containing ExecutionOutputLink and parameters of FuzzyWithin
    Handle hListLink = atomSpace.getOutgoing(hFuzzyWithin, 1);

    if ( hListLink == opencog::Handle::UNDEFINED ||
         atomSpace.getType(hListLink) != LIST_LINK || 
         atomSpace.getArity(hListLink) != 3 ) {

        logger().error("PsiDemandUpdaterAgent::Demand::%s - Expect a ListLink for demand '%s' with three arity (parameters of FuzzyWithin). But got '%s'", 
                       __FUNCTION__, 
                       this->demandName.c_str(), 
                       atomSpace.atomAsString(hListLink).c_str()
                      );
        return false; 
    }

    // Get the ExecutionOutputLink
    Handle hExecutionOutputLink = atomSpace.getOutgoing(hListLink, 2);

    if ( hExecutionOutputLink == opencog::Handle::UNDEFINED ||
         atomSpace.getType(hExecutionOutputLink) != EXECUTION_OUTPUT_LINK ||
         atomSpace.getArity(hExecutionOutputLink) != 2 ) {

        logger().error("PsiDemandUpdaterAgent::Demand::%s - Expect a ExecutionOutputLink for demand '%s' with two arity that contains an updater. But got '%s'", 
                       __FUNCTION__, 
                       this->demandName.c_str(), 
                       atomSpace.atomAsString(hExecutionOutputLink).c_str()
                      );
        return false; 
    }

    // Create a new NumberNode and SilarityLink to store the result
    //
    // Note: Since OpenCog would forget (remove) those Nodes and Links gradually, 
    //       unless you create them to be permanent, don't worry about the overflow of memory. 

    // Create a new NumberNode that stores the updated value
    // If the Demand doesn't change at all, it actually returns the old one
    Handle hNewNumberNode = AtomSpaceUtil::addNode( atomSpace,
                                                    NUMBER_NODE,
                                                    boost::lexical_cast<std::string>
                                                           (this->currentDemandValue),
                                                    false // the atom should not be permanent (can be
                                                          // removed by decay importance task) 
                                                  );

    // Create a new SimilarityLink that holds the DemandSchema
    // If the Demand doesn't change at all, it actually returns the old one
    std::vector<Handle> similarityLinkOutgoing;

    similarityLinkOutgoing.push_back(hNewNumberNode);
    similarityLinkOutgoing.push_back(hExecutionOutputLink);

    Handle hNewSimilarityLink = AtomSpaceUtil::addLink( atomSpace,
                                                        SIMILARITY_LINK, 
                                                        similarityLinkOutgoing,
                                                        false // the atom should not be permanent (can be
                                                              // removed by decay importance task)
                                                      );
  
    // Time stamp the SimilarityLink.
    Handle hAtTimeLink = atomSpace.getTimeServer().addTimeInfo(hNewSimilarityLink, timeStamp);

    logger().debug("PsiDemandUpdaterAgent::Demand::%s - Updated the value of '%s' demand to %f and store it to AtomSpace as '%s'", 
                   __FUNCTION__, 
                   demandName.c_str(), 
                   this->currentDemandValue, 
                   atomSpace.atomAsString(hNewSimilarityLink).c_str()
                  );

    // Run the FuzzyWithin procedure
    std::string demandGoalEvaluator = atomSpace.getName(hGroundedPredicateNode);
    std::vector <combo::vertex> schemaArguments;
    Procedure::RunningProcedureID executingSchemaId;
    combo::vertex result; // combo::vertex is actually of type boost::variant <...>

    // Get min/ max acceptable values
    std::string minValueStr = atomSpace.getName( atomSpace.getOutgoing(hListLink, 0) );
    std::string maxValueStr = atomSpace.getName( atomSpace.getOutgoing(hListLink, 1) );

    // Prepare arguments
    schemaArguments.clear(); 

    // TODO: we would also use previous demand values in future
    schemaArguments.push_back( this->currentDemandValue);  
    schemaArguments.push_back( boost::lexical_cast<double> (minValueStr) );
    schemaArguments.push_back( boost::lexical_cast<double> (maxValueStr) );

    // Run the procedure
    const Procedure::GeneralProcedure & procedure =
                                        procedureRepository.get(demandGoalEvaluator);

    executingSchemaId = procedureInterpreter.runProcedure(procedure, schemaArguments);

    // Wait until the procedure done
    while ( !procedureInterpreter.isFinished(executingSchemaId) )
        procedureInterpreter.run(NULL);  

    // Check if the the FuzzyWithin procedure run successfully
    if ( procedureInterpreter.isFailed(executingSchemaId) ) {
        logger().error( "PsiDemandUpdaterAgent::Demand::%s - Failed to execute FuzzyWithin procedure '%s' for demand '%s'", 
                         __FUNCTION__, 
                         demandGoalEvaluator.c_str(), 
                         this->demandName.c_str()
                      );

        return false;
    }

    result = procedureInterpreter.getResult(executingSchemaId);

    // Store the result and update TruthValue of EvaluationLinkDemandGoal and EvaluationLinkFuzzyWithin
    // TODO: Use PLN forward chainer to handle this?
    atomSpace.setTV( this->hDemandGoal,
                     SimpleTruthValue(get_contin(result), 1.0f)
                   );

    atomSpace.setTV( this->hFuzzyWithin,
                     SimpleTruthValue(get_contin(result), 1.0f)
                   );

    this->currentDemandTruthValue = get_contin(result);

    logger().debug( "PsiDemandUpdaterAgent::Demand::%s - The level  (truth value) of DemandGoal '%s' has been set to %f", 
                     __FUNCTION__, 
                     this->demandName.c_str(),
                     get_contin(result)
                  );

    return true; 
}    

PsiDemandUpdaterAgent::~PsiDemandUpdaterAgent()
{
#ifdef HAVE_ZMQ
    delete this->publisher; 
#endif
}

PsiDemandUpdaterAgent::PsiDemandUpdaterAgent()
{
    this->cycleCount = 0;

    // Force the Agent initialize itself during its first cycle. 
    this->forceInitNextCycle();
}

#ifdef HAVE_ZMQ
void PsiDemandUpdaterAgent::publishUpdatedValue(Plaza & plaza, 
                                                zmq::socket_t & publisher, 
                                                const unsigned long timeStamp)
{
    using namespace json_spirit; 

    // Send the name of current mind agent which would be used as a filter key by subscribers
    std::string keyString = "PsiDemandUpdaterAgent"; 
    plaza.publishStringMore(publisher, keyString); 

    // Pack time stamp and all the Demand values in json format 
    Object jsonObj; // json_spirit::Object is of type std::vector< Pair >
    jsonObj.push_back( Pair("timestamp", (uint64_t) timeStamp) );

    foreach (Demand & demand, this->demandList) {
        jsonObj.push_back( Pair( demand.getDemandName()+"TruthValue", demand.getDemandTruthValue() ) );
    }

    // Publish the data packed in json format
    std::string dataString = write_formatted(jsonObj);
    plaza.publishString(publisher, dataString);
}
#endif // HAVE_ZMQ

void PsiDemandUpdaterAgent::init(opencog::CogServer * server) 
{
    logger().debug( "PsiDemandUpdaterAgent::%s - Initialize the Agent [cycle = %d]",
                    __FUNCTION__, 
                    this->cycleCount
                  );

    // Get OAC
    OAC * oac = (OAC *) server;

    // Get AtomSpace
    const AtomSpace & atomSpace = * ( oac->getAtomSpace() );

    // Get Procedure repository
    const Procedure::ProcedureRepository & procedureRepository = 
                                               oac->getProcedureRepository();

    // Get petId
    const std::string & petId = oac->getPet().getPetId();

    // Clear old demandList 
    this->demandList.clear();

    // Get demand names from the configuration file
    std::string demandNames = config()["PSI_DEMANDS"];

    // Process Demands one by one
    boost::char_separator<char> sep(", ");
    boost::tokenizer< boost::char_separator<char> > demandNamesTok (demandNames, sep);

    std::string demandName, demandUpdater;
    Handle hDemandGoal, hFuzzyWithin;

    // Process Relations one by one 
    for ( boost::tokenizer< boost::char_separator<char> >::iterator iDemandName = demandNamesTok.begin();
          iDemandName != demandNamesTok.end();
          iDemandName ++ ) {

        demandName = (*iDemandName);
        demandUpdater = demandName + "DemandUpdater";

        // Search demand updater
        if ( !procedureRepository.contains(demandUpdater) ) {
            logger().warn( "PsiDemandUpdaterAgent::%s - Failed to find '%s' in OAC's procedureRepository [cycle = %d]",
                           __FUNCTION__, 
                           demandUpdater.c_str(), 
                           this->cycleCount
                         );
            continue;
        }
    
        // Search the corresponding SimultaneousEquivalenceLink
        if ( !AtomSpaceUtil::getDemandEvaluationLinks(atomSpace, demandName, hDemandGoal, hFuzzyWithin) )
        {
            logger().warn( "PsiDemandUpdaterAgent::%s - Failed to get EvaluationLinks for demand '%s' [cycle = %d]",
                           __FUNCTION__, 
                           demandName.c_str(), 
                           this->cycleCount
                         );

            continue;
        }

        this->demandList.push_back(Demand(demandName, hDemandGoal, hFuzzyWithin));

        logger().debug("PsiDemandUpdaterAgent::%s - Store the meta data of demand '%s' successfully [cycle = %d]", 
                        __FUNCTION__, 
                        demandName.c_str(), 
                        this->cycleCount
                      );
    }// for

    // Initialize ZeroMQ publisher and add it to the plaza
#ifdef HAVE_ZMQ
    Plaza & plaza = oac->getPlaza();
    this->publisher = new zmq::socket_t (plaza.getZmqContext(), ZMQ_PUB);
    this->publishEndPoint = "ipc://" + petId + ".PsiDemandUpdaterAgent.ipc"; 
    this->publisher->bind( this->publishEndPoint.c_str() );

    plaza.addPublisher(this->publishEndPoint); 
#endif    

    // Avoid initialize during next cycle
    this->bInitialized = true;
}

void PsiDemandUpdaterAgent::run(opencog::CogServer * server)
{
    this->cycleCount ++;

    logger().debug( "PsiDemandUpdaterAgent::%s - Executing run %d times",
                     __FUNCTION__, 
                     this->cycleCount
                  );

    // Get OAC
    OAC * oac = (OAC *) server;

    // Get AtomSpace
    AtomSpace & atomSpace = * ( oac->getAtomSpace() );

    // Get Pet
    Pet & pet = oac->getPet();

    // Get petId
    const std::string & petId = pet.getPetId();

    // Get ProcedureInterpreter
    Procedure::ProcedureInterpreter & procedureInterpreter = oac->getProcedureInterpreter();

    // Get Procedure repository
    const Procedure::ProcedureRepository & procedureRepository = oac->getProcedureRepository();

    // Get current time stamp
    unsigned long timeStamp = atomSpace.getTimeServer().getLatestTimestamp();

    // Check if map info data is available
    if ( atomSpace.getSpaceServer().getLatestMapHandle() == Handle::UNDEFINED ) {
        logger().warn( 
      "PsiDemandUpdaterAgent::%s - There is no map info available yet [cycle = %d]", 
                        __FUNCTION__, 
                        this->cycleCount
                     );
        return;
    }

    // Check if the pet spatial info is already received
    if ( !atomSpace.getSpaceServer().getLatestMap().containsObject(petId) ) {
        logger().warn(
 "PsiDemandUpdaterAgent::%s - Pet was not inserted in the space map yet [cycle = %d]", 
                     __FUNCTION__, 
                     this->cycleCount
                     );
        return;
    }

    // Initialize the Agent (demandList etc)
    if ( !this->bInitialized )
        this->init(server);

    // Update demand values
    foreach (Demand & demand, this->demandList) {
        logger().debug("PsiDemandUpdaterAgent::%s - Going to run updaters for demand '%s' [cycle = %d]", 
                       __FUNCTION__, 
                       demand.getDemandName().c_str(), 
                       this->cycleCount
                      );

        demand.runUpdater(atomSpace, procedureInterpreter, procedureRepository);
    }

    // Update Demand Goals
    foreach (Demand & demand, this->demandList) {
        logger().debug("PsiDemandUpdaterAgent::%s - Going to set the updated value to AtomSpace for demand '%s' [cycle = %d]", 
                       __FUNCTION__, 
                       demand.getDemandName().c_str(), 
                       this->cycleCount
                      );

        demand.updateDemandGoal(atomSpace, procedureInterpreter, procedureRepository, timeStamp);
    }

#ifdef HAVE_ZMQ    
    // Publish updated Demand values via ZeroMQ
    Plaza & plaza = oac->getPlaza();
    this->publishUpdatedValue(plaza, *this->publisher, timeStamp); 
#endif 

    // Update the truth value of previous/ current demand goal
    if ( pet.getPreviousDemandGoal() != opencog::Handle::UNDEFINED ) {
        logger().debug("PsiDemandUpdaterAgent::%s - Previous demand goal stored in pet is '%s' [cycle = %d]", 
                       __FUNCTION__, 
                       atomSpace.atomAsString(pet.getPreviousDemandGoal()).c_str(), 
                       this->cycleCount
                      );

        atomSpace.setTV( AtomSpaceUtil::getDemandGoalEvaluationLink(atomSpace, PREVIOUS_DEMAND_NAME), 
                         * atomSpace.getTV( pet.getPreviousDemandGoal() )
                       );

        logger().debug("PsiDemandUpdaterAgent::%s - Previous demand goal has been updated to: '%s' [cycle = %d]", 
                       __FUNCTION__, 
                       atomSpace.atomAsString(AtomSpaceUtil::getDemandGoalEvaluationLink(atomSpace, PREVIOUS_DEMAND_NAME)).c_str(), 
                       this->cycleCount
                      );
    }

    if ( pet.getCurrentDemandGoal() != opencog::Handle::UNDEFINED ) {
        logger().debug("PsiDemandUpdaterAgent::%s - Current demand goal stored in pet is '%s' [cycle = %d]", 
                       __FUNCTION__, 
                       atomSpace.atomAsString(pet.getCurrentDemandGoal()).c_str(), 
                       this->cycleCount
                      );


        atomSpace.setTV( AtomSpaceUtil::getDemandGoalEvaluationLink(atomSpace, CURRENT_DEMAND_NAME), 
                         * atomSpace.getTV( pet.getCurrentDemandGoal() )
                       );

        logger().debug("PsiDemandUpdaterAgent::%s - Current demand goal has been updated to: '%s' [cycle = %d]", 
                       __FUNCTION__, 
                       atomSpace.atomAsString(AtomSpaceUtil::getDemandGoalEvaluationLink(atomSpace, CURRENT_DEMAND_NAME)).c_str(), 
                       this->cycleCount
                      );

    }

}

