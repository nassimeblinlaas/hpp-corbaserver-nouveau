/*
  Research carried out within the scope of the Associated International Laboratory: Joint Japanese-French Robotics Laboratory (JRL)

  Developed by Florent Lamiraux (LAAS-CNRS)

*/

#include <errno.h>
#include <pthread.h>
#include <iostream>
#include "hppCorbaServer/hppciServer.h"
#include "hppCorbaServer/hppciServerPrivate.h"
#include "hppciExceptionHandlingMacros.h"

// Select verbosity at configuration by setting CXXFLAGS="... -DDEBUG=[1 or 2]"
#if DEBUG==2
#define ODEBUG2(x) std::cout << "hppciServer.cpp:" << x << std::endl
#define ODEBUG1(x) std::cerr << "hppciServer.cpp:" << x << std::endl
#elif DEBUG==1
#define ODEBUG2(x)
#define ODEBUG1(x) std::cerr << "hppciServer.cpp:" << x << std::endl
#else
#define ODEBUG2(x)
#define ODEBUG1(x)
#endif


ChppciServer* ChppciServer::s_hppciServer;

ChppciServer::ChppciServer(ChppPlanner *inHppPlanner, int argc, char *argv[]) : 
  hppPlanner(inHppPlanner)
{
  s_hppciServer = this, 
  attPrivate = new ChppciServerPrivate;

  initORBandServers(argc, argv);
  initMapSteeringMethodFactory();
  initMapDistanceFunctionFactory();
}

/// \brief Shutdown CORBA server
ChppciServer::~ChppciServer()
{
  attPrivate->deactivateAndDestroyServers();
  attPrivate->orb->shutdown(0);
  delete attPrivate;
  attPrivate = NULL;
  s_hppciServer = NULL;
  destroySteeringMethodFactories();
}

ChppciServer* ChppciServer::getInstance()
{
  return s_hppciServer;
}

/*
            STEERING METHOD FACTORIES
*/

void ChppciServer::initMapSteeringMethodFactory()
{
  attMapSteeringMethodFactory["linear"] = new CkwsPlusLinearSteeringMethodFactory;
  attMapSteeringMethodFactory["rs"] = new CkwsPlusRSSteeringMethodFactory(1.0);
  attMapSteeringMethodFactory["flic"] = new CkwsPlusFlicSteeringMethodFactory();
}

void ChppciServer::destroySteeringMethodFactories()
{
  std::map<std::string, CkwsPlusSteeringMethodFactory*>::iterator start 
    = attMapSteeringMethodFactory.begin();
  std::map<std::string, CkwsPlusSteeringMethodFactory*>::iterator end
    = attMapSteeringMethodFactory.end();

  for (std::map<std::string, CkwsPlusSteeringMethodFactory*>::iterator it=start;
       it != end; it++) {
    CkwsPlusSteeringMethodFactory* factory = it->second;
    ODEBUG2("deleting steering method factory" << it->first);
    delete factory;
  }
}

bool ChppciServer::steeringMethodFactoryAlreadySet(std::string inName)
{
  if (attMapSteeringMethodFactory.count(inName) == 1) {
    return true;
  }
  return false;
}


bool ChppciServer::addSteeringMethodFactory(std::string inName, 
					    CkwsPlusSteeringMethodFactory* inSteeringMethodFactory)
{
  if(steeringMethodFactoryAlreadySet(inName)) {
    return false;
  }
  attMapSteeringMethodFactory[inName] = inSteeringMethodFactory;
  return true;
}

CkwsSteeringMethodShPtr ChppciServer::createSteeringMethod(std::string inName,
							   bool inOriented)
{
  CkwsSteeringMethodShPtr result;

  if (steeringMethodFactoryAlreadySet(inName)) {
    result = attMapSteeringMethodFactory[inName]->makeSteeringMethod(inOriented);
  }
  return result;
}

/*
            DISTANCE FUNCTION FACTORIES
*/

void ChppciServer::initMapDistanceFunctionFactory()
{
  attMapDistanceFunctionFactory["linear"] = new CkwsPlusLinearDistanceFactory;
  attMapDistanceFunctionFactory["rs"] = new CkwsPlusRSDistanceFactory(1.0);
  attMapDistanceFunctionFactory["flic"] = new CkwsPlusApproxFlicDistanceFactory;
}

void ChppciServer::destroyDistanceFunctionFactories()
{
  std::map<std::string, CkwsPlusDistanceFactory*>::iterator start 
    = attMapDistanceFunctionFactory.begin();
  std::map<std::string, CkwsPlusDistanceFactory*>::iterator end
    = attMapDistanceFunctionFactory.end();

  for (std::map<std::string, CkwsPlusDistanceFactory*>::iterator it=start;
       it != end; it++) {
    CkwsPlusDistanceFactory* factory = it->second;
    ODEBUG2(" deleting distance function factory" << it->first);
    delete factory;
  }
}

bool ChppciServer::distanceFactoryAlreadySet(std::string inName)
{
  if (attMapDistanceFunctionFactory.count(inName) == 1) {
    return true;
  }
  return false;
}


bool ChppciServer::addDistanceFactory(std::string inName, 
				      CkwsPlusDistanceFactory* inDistanceFunctionFactory)
{
  if(distanceFactoryAlreadySet(inName)) {
    return false;
  }
  attMapDistanceFunctionFactory[inName] = inDistanceFunctionFactory;
  return true;
}

CkwsDistanceShPtr ChppciServer::createDistanceFunction(std::string inName,
						       bool inOriented)
{
  CkwsDistanceShPtr result;

  if (distanceFactoryAlreadySet(inName)) {
    result = attMapDistanceFunctionFactory[inName]->makeDistance(inOriented);
  }
  return result;
}



/*
            CORBA SERVER INITIALIZATION
*/

ktStatus ChppciServer::initORBandServers(int argc, char *argv[])
{
  CORBA::Object_var obj;
  PortableServer::ThreadPolicy_var singleThread;
  PortableServer::POA_var rootPoa;

  /* 
     Fine granularity in exception handling
  */

  /*
    ORB init
  */
  try {
    attPrivate->orb = CORBA::ORB_init(argc, argv);
    if (CORBA::is_nil(attPrivate->orb)) {
      std::cerr << "hppCorbaServer: failed to initialize ORB" << std::endl;
      return KD_ERROR;
    }
  }
  HPPCI_CATCH("failed to initialize ORB", KD_ERROR) /* see hppciExceptionHandlingMacros.h */

  /*
    ORB init
  */

  try {
    obj = attPrivate->orb->resolve_initial_references("RootPOA");    
  }
  HPPCI_CATCH("failed to resolve initial references", KD_ERROR) /* see hppciExceptionHandlingMacros.h */
  
  /*
    Create thread policy
  */

  try {
    //
    // Make the CORBA object single-threaded to avoid GUI krash
    //
    // Create a sigle threaded policy object
    rootPoa = PortableServer::POA::_narrow(obj);
    singleThread = rootPoa->create_thread_policy(PortableServer::MAIN_THREAD_MODEL);
  }
  HPPCI_CATCH("failed to create thread policy", KD_ERROR) /* see hppciExceptionHandlingMacros.h */
  
  /*
    Duplicate thread policy
  */

  try {
    CORBA::PolicyList policyList;
    policyList.length(1);
    policyList[0] = PortableServer::ThreadPolicy::_duplicate(singleThread);

    attPrivate->poa = rootPoa->create_POA("child", PortableServer::POAManager::_nil(),
					  policyList);

  }
  HPPCI_CATCH("failed to duplicate thread policy", KD_ERROR) /* see hppciExceptionHandlingMacros.h */
  
  /*
    Destroy thread policy
  */

  try {
    // Destroy policy object
    singleThread->destroy();

  }
  HPPCI_CATCH("failed to destroy thread policy", KD_ERROR); /* see hppciExceptionHandlingMacros.h */
  
  return attPrivate->createAndActivateServers(this);
}

int ChppciServer::startCorbaServer()
{
  try {
    // Obtain a reference to objects, and register them in
    // the naming service.
    CORBA::Object_var robotObj = attPrivate->robotServant->_this();
    CORBA::Object_var obstacleObj = attPrivate->obstacleServant->_this();
    CORBA::Object_var problemObj = attPrivate->problemServant->_this();

    if (!attPrivate->createHppContext()) {
      return KD_ERROR;
    }
    // Bind robotObj with name Robot to the hppContext:
    CosNaming::Name objectName;
    objectName.length(1);
    objectName[0].id   = (const char*) "Robot";   // string copied
    objectName[0].kind = (const char*) "Object"; // string copied

    if(!attPrivate->bindObjectToName(robotObj, objectName)) {
      return KD_ERROR;
    }
    attPrivate->robotServant->_remove_ref();

    // Bind obstacleObj with name Obstacle to the hppContext:
    objectName.length(1);
    objectName[0].id   = (const char*) "Obstacle";   // string copied
    objectName[0].kind = (const char*) "Object"; // string copied

    if(!attPrivate->bindObjectToName(obstacleObj, objectName)) {
      return KD_ERROR;
    }
    attPrivate->obstacleServant->_remove_ref();

    // Bind problemObj with name Problem to the hppContext:
    objectName.length(1);
    objectName[0].id   = (const char*) "Problem";   // string copied
    objectName[0].kind = (const char*) "Object"; // string copied

    if(!attPrivate->bindObjectToName(problemObj, objectName)) {
      return KD_ERROR;
    }
    attPrivate->problemServant->_remove_ref();

    PortableServer::POAManager_var pman = attPrivate->poa->the_POAManager();
    pman->activate();
  }
  HPPCI_CATCH("failed to start CORBA server", KD_ERROR);
  return KD_OK;
}

ChppPlanner *ChppciServer::getHppPlanner()
{
  return hppPlanner;
}



/// \brief If CORBA requests are pending, process them
int ChppciServer::processRequest(bool loop)
{
  if (loop) {
    // Enter in the Corba control loop. Never return.
    cout << "ChppciServer::processRequest: attPrivate->orb->run();" << endl;
    attPrivate->orb->run();
  } else {
    if (attPrivate->orb->work_pending()) {
      attPrivate->orb->perform_work();
    }
  }
    return 0;
}

