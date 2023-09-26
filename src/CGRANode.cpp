/*
 * ======================================================================
 * CGRANode.cpp
 * ======================================================================
 * CGRA tile implementation.
 *
 * Author : Cheng Tan
 *   Date : July 16, 2019
 */

#include "CGRANode.h"
#include <stdio.h>

#define SINGLE_OCCUPY     0 // A single-cycle opt is in the FU
#define START_PIPE_OCCUPY 1 // A multi-cycle opt starts in the FU
#define END_PIPE_OCCUPY   2 // A multi-cycle opt ends in the FU
#define IN_PIPE_OCCUPY    3 // A multi-cycle opt is occupying the FU

//CGRANode::CGRANode(int t_id) {
//  m_id = t_id;
//  m_currentCtrlMemItems = 0;
//  m_canStore = false;
//  m_canLoad = false;
//}

CGRANode::CGRANode(int t_id, int t_x, int t_y) {
  m_id = t_id;
  m_currentCtrlMemItems = 0;
  m_disabled = false;
  m_canStore = false;
  m_canLoad = false;
  m_x = t_x;
  m_y = t_y;
  m_neighbors = NULL;
  m_occupiableInLinks = NULL;
  m_occupiableOutLinks = NULL;
  // new list<list<pair<DFGNode*, int>>*>();//DFGNode*[1];
  // m_dfgNodes = new DFGNode*[1];
  // m_fuOccupied = new int[1];
  m_regs_duration = NULL;
  m_regs_timing = NULL;

  // used for parameterizable CGRA functional units
  m_canCall   = true;
  m_canAdd    = true;
  m_canMul    = true;
  m_canShift  = true;
  m_canPhi    = true;
  m_canSel    = true;
  m_canCmp    = true;
  m_canMAC    = true;
  m_canLogic  = true;
  m_canBr     = true;
  m_canReturn = true;
}

// FIXME: should handle the case that the data is maintained in the registers
//        for multiple cycles.
void CGRANode::allocateReg(CGRALink* t_link, int t_cycle, int t_duration, int t_II) {
  int reg_id = t_link->getDirectionID(this);
  allocateReg(reg_id, t_cycle, t_duration, t_II);
}

void CGRANode::allocateReg(int t_port_id, int t_cycle, int t_duration, int t_II) {
  bool allocated = false;
  for (int i=0; i<m_registerCount; ++i) {
    bool reg_occupied = false;
    for (int cycle=t_cycle; cycle<m_cycleBoundary; cycle+=t_II) {
      for (int d=0; d<t_duration; ++d) {
        if (cycle+d<m_cycleBoundary and m_regs_duration[cycle+d][i] != -1)
          reg_occupied = true;
      }
    }
    for (int cycle=t_cycle; cycle>=0; cycle-=t_II) {
      for (int d=0; d<t_duration; ++d) {
        if (m_regs_duration[cycle+d][i] != -1)
          reg_occupied = true;
      }
    }
    if (reg_occupied == false) {
      cout<<"[DEBUG] in allocateReg() t_cycle: "<<t_cycle<<"; i: "<<i<<" CGRA node: "<<this->getID()<<"; link: "<<t_port_id<<" duration "<<t_duration<<"\n";
      for (int cycle=t_cycle; cycle<m_cycleBoundary; cycle+=t_II) {
        m_regs_timing[cycle][i] = t_port_id;
        for (int d=0; d<t_duration; ++d) {
          if (cycle+d<m_cycleBoundary) {
            // assert(m_regs_duration[cycle+d][i] == -1);
            m_regs_duration[cycle+d][i] = t_port_id;
          }
        }
      }
      for (int cycle=t_cycle; cycle>=0; cycle-=t_II) {
        m_regs_timing[cycle][i] = t_port_id;
        for (int d=0; d<t_duration; ++d) {
          m_regs_duration[cycle+d][i] = t_port_id;
        }
      }
      allocated = true;
      break;
    }
  }
  cout<<"[DEBUG] done reg allocation"<<endl;
  //assert(allocated);
}

int* CGRANode::getRegsAllocation(int t_cycle) {
  return m_regs_timing[t_cycle];
}

void CGRANode::setCtrlMemConstraint(int t_ctrlMemConstraint) {
  m_ctrlMemSize = t_ctrlMemConstraint;
}

void CGRANode::setRegConstraint(int t_registerConstraint) {
  m_registerCount = t_registerConstraint;
}

void CGRANode::setID(int t_id) {
  m_id = t_id;
}

void CGRANode::setLocation(int t_x, int t_y) {
  m_x = t_x;
  m_y = t_y;
}

int CGRANode::getID() {
  return m_id;
}

void CGRANode::attachInLink(CGRALink* t_link) {
  m_inLinks.push_back(t_link);
}

void CGRANode::attachOutLink(CGRALink* t_link) {
  m_outLinks.push_back(t_link);
}

list<CGRALink*>* CGRANode::getInLinks() {
  return &m_inLinks;
}

list<CGRALink*>* CGRANode::getOutLinks() {
  return &m_outLinks;
}

list<CGRANode*>* CGRANode::getNeighbors() {
  if (m_neighbors != NULL)
    return m_neighbors;
  m_neighbors = new list<CGRANode*>();
  for (CGRALink* link: m_outLinks)
    m_neighbors->push_back(link->getConnectedNode(this));
  return m_neighbors;
}

/**
 * What is in this Function:
 * 1. set m_cycleBoundary a big value
 * 2. init a space for m_dfgNodesWithOccupyStatus according the m_cycleBoundary
 * 3. init space for m_regs_duration and m_regs_timing according the m_cycleBoundary
 */
void CGRANode::constructMRRG(int t_CGRANodeCount, int t_II) {
  m_cycleBoundary = t_CGRANodeCount*t_II*t_II; //give a big value
  m_currentCtrlMemItems = 0;
  m_registers.clear();
  // Delete all these local arrays to avoid memory leakage.
  if (m_dfgNodesWithOccupyStatus.size() > 0) {
    for (list<pair<DFGNode*, int>>* opts: m_dfgNodesWithOccupyStatus) {
      opts->clear();
    }
  }
  m_dfgNodesWithOccupyStatus.clear();
  for (int i=0; i<m_cycleBoundary; ++i) {
    m_dfgNodesWithOccupyStatus.push_back(new list<pair<DFGNode*, int>>());
  }
  
  m_regs_duration = new int*[m_cycleBoundary];
  m_regs_timing = new int*[m_cycleBoundary];
  for (int i=0; i<m_cycleBoundary; ++i) {
    m_regs_duration[i] = new int[m_registerCount];
    m_regs_timing[i] = new int[m_registerCount];
    for (int j=0; j<m_registerCount; ++j) {
      m_regs_duration[i][j] = -1;
      m_regs_timing[i][j] = -1;
    }
  }
}

bool CGRANode::canSupport(DFGNode* t_opt) {
  if (m_disabled) 
    return false;
  // Check whether this CGRA node supports the required functionality.
  if ((t_opt->isLoad()       and !canLoad())  or
      (t_opt->isStore()      and !canStore()) or
      (t_opt->isReturn()     and !canReturn()) or
      (t_opt->isCall()       and !canCall())){
    return false;
  }
  return true;
}

bool CGRANode::canOccupy(DFGNode* t_opt, int t_cycle, int t_II) {
  if (m_disabled) 
    return false;

  // Check whether this CGRA node supports the required functionality.
  if (!canSupport(t_opt)) {
    return false;
  }

  // Check whether the limit of config mem is reached.
  if (m_currentCtrlMemItems + 1 > m_ctrlMemSize) {
    return false;
  }

  // Handle multi-cycle execution and pipelinable operations.
  if (not t_opt->isMultiCycleExec()) {
    // Single-cycle opt:
    for (int cycle=t_cycle%t_II; cycle<m_cycleBoundary; cycle+=t_II) {
      for (pair<DFGNode*, int> p: *(m_dfgNodesWithOccupyStatus[cycle])) {
        if (p.second != IN_PIPE_OCCUPY) {
          return false;
        }
      }
    }
  } else {
    // Multi-cycle opt.
    for (int cycle=t_cycle%t_II; cycle<m_cycleBoundary; cycle+=t_II) {
      // Check start cycle.
      for (pair<DFGNode*, int> p: *(m_dfgNodesWithOccupyStatus[cycle])) {
        // Multi-cycle opt's start cycle overlaps with single-cycle opt' cycle.
        if (p.second == SINGLE_OCCUPY) {
          return false;
        } 
        // Multi-cycle opt's start cycle overlaps with multi-cycle opt's start cycle.
        else if (p.second == START_PIPE_OCCUPY) {
          return false;
        }
        // Multi-cycle opt's start cycle overlaps with multi-cycle opt with the same type:
        else if ((p.second == IN_PIPE_OCCUPY or p.second == END_PIPE_OCCUPY) and
                 (t_opt->shareFU(p.first))   and
                 (not t_opt->isPipelinable() or not p.first->isPipelinable())) {
          return false;
        }
      }
      if (cycle+t_opt->getExecLatency()-1 >= m_cycleBoundary) {
        break;
      }
      // Check end cycle.
      for (pair<DFGNode*, int> p: *(m_dfgNodesWithOccupyStatus[cycle+t_opt->getExecLatency()-1])) {
        // Multi-cycle opt's end cycle overlaps with single-cycle opt' cycle.
        if (p.second == SINGLE_OCCUPY) {
          return false;
        } 
        // Multi-cycle opt's end cycle overlaps with multi-cycle opt's end cycle.
        else if (p.second == END_PIPE_OCCUPY) {
          return false;
        }
        // Multi-cycle opt's end cycle overlaps with multi-cycle opt with the same type:
        else if ((p.second == IN_PIPE_OCCUPY or p.second == START_PIPE_OCCUPY) and
                 (t_opt->shareFU(p.first))   and
                 (not t_opt->isPipelinable() or not p.first->isPipelinable())) {
          return false;
        }
      }
    }
  }

  return true;
}

bool CGRANode::isOccupied(int t_cycle, int t_II) {
  for (int cycle=t_cycle; cycle<m_cycleBoundary; cycle+=t_II) {
    for (pair<DFGNode*, int> p: *(m_dfgNodesWithOccupyStatus[cycle])) {
      // if (m_fuOccupied[cycle])
      if (p.second == START_PIPE_OCCUPY or p.second == SINGLE_OCCUPY) {
        return true;
      }
    }
  }
  return false;
}

void CGRANode::setDFGNode(DFGNode* t_opt, int t_cycle, int t_II,
    bool t_isStaticElasticCGRA) {
  int interval = t_II;
  if (t_isStaticElasticCGRA) {
    interval = 1;
  }
  for (int cycle=t_cycle%interval; cycle<m_cycleBoundary; cycle+=interval) {
    if (not t_opt->isMultiCycleExec()) {
      m_dfgNodesWithOccupyStatus[cycle]->push_back(make_pair(t_opt, SINGLE_OCCUPY));
    } else {
      m_dfgNodesWithOccupyStatus[cycle]->push_back(make_pair(t_opt, START_PIPE_OCCUPY));
      for (int i=1; i<t_opt->getExecLatency()-1; ++i) {
        if (cycle+i < m_cycleBoundary) {
          m_dfgNodesWithOccupyStatus[cycle+i]->push_back(make_pair(t_opt, IN_PIPE_OCCUPY));
        }
      }
      int lastCycle = cycle+t_opt->getExecLatency()-1;
      if (lastCycle < m_cycleBoundary) {
        m_dfgNodesWithOccupyStatus[lastCycle]->push_back(make_pair(t_opt, END_PIPE_OCCUPY));
      }
    }
  }

  cout<<"[DEBUG] setDFGNode "<<t_opt->getID()<<" onto CGRANode "<<getID()<<" at cycle: "<<t_cycle<<"\n";
  ++m_currentCtrlMemItems;
  t_opt->setMapped();
}

DFGNode* CGRANode::getMappedDFGNode(int t_cycle) {
  for (pair<DFGNode*, int> p: *(m_dfgNodesWithOccupyStatus[t_cycle])) {
    if (p.second == SINGLE_OCCUPY or p.second == END_PIPE_OCCUPY) {
      return p.first;
    }
  }
  return NULL;
}

bool CGRANode::containMappedDFGNode(DFGNode* t_node, int t_II) {
  for (int c=0; c<2*t_II; ++c) {
    for (pair<DFGNode*, int> p: *(m_dfgNodesWithOccupyStatus[c])) {
      if (t_node == p.first) {
        return true;
      }
    }
  }
  return false;
}

void CGRANode::configXbar(CGRALink*, int, int)
{

}

void CGRANode::addRegisterValue(float t_value) {
  m_registers.push_back(t_value);
}

list<CGRALink*>* CGRANode::getOccupiableInLinks(int t_cycle, int t_II) {
  if (m_occupiableInLinks == NULL)
    m_occupiableInLinks = new list<CGRALink*>();
  m_occupiableInLinks->clear();
  for (CGRALink* link: m_inLinks) {
    if (link->canOccupy(t_cycle, t_II)) {
      m_occupiableInLinks->push_back(link);
    }
  }
  return m_occupiableInLinks;
}

list<CGRALink*>* CGRANode::getOccupiableOutLinks(int t_cycle, int t_II) {
  if (m_occupiableOutLinks == NULL)
    m_occupiableOutLinks = new list<CGRALink*>();
  m_occupiableOutLinks->clear();
  for (CGRALink* link: m_outLinks) {
    if (link->canOccupy(t_cycle, t_II)) {
      m_occupiableOutLinks->push_back(link);
    }
  }
  return m_occupiableOutLinks;
}

int CGRANode::getAvailableRegisterCount() {
  return (m_registerCount - m_registers.size());
}

CGRALink* CGRANode::getInLink(CGRANode* t_node) {
  for (CGRALink* link: m_inLinks) {
    if (link->getSrc() == t_node) {
      return link;
    }
  }
  // will definitely return one inlink
  assert(0);
}

CGRALink* CGRANode::getOutLink(CGRANode* t_node) {
  for (CGRALink* link: m_outLinks) {
    if (link->getDst() == t_node)
      return link;
  }
  return NULL;
  // will definitely return one outlink
//  assert(0);
}

int CGRANode::getMinIdleCycle(DFGNode* t_dfgNode, int t_cycle, int t_II) {
  int tempCycle = t_cycle;
  while (tempCycle < m_cycleBoundary) {
    if (canOccupy(t_dfgNode, tempCycle, t_II))
      return tempCycle;
    ++tempCycle;
  }
  return m_cycleBoundary;
}

int CGRANode::getCurrentCtrlMemItems() {
  return m_currentCtrlMemItems;
}

// TODO: will support precision-based operations (e.g., fadd, fmul, etc).
bool CGRANode::enableFunctionality(string t_func) {
  if (t_func.compare("store")) {
    enableStore();
  } else if (t_func.compare("load")) {
    enableLoad();
  } else if (t_func.compare("return")) {
    enableReturn();
  } else if (t_func.compare("call")) {
    enableCall();
  } else {
    return false;
  }
  return true;
}

void CGRANode::enableReturn() {
  m_canReturn = true;
}

void CGRANode::enableStore() {
  m_canStore = true;
}

void CGRANode::enableLoad() {
  m_canLoad = true;
}

void CGRANode::enableCall() {
  m_canCall = true;
}

void CGRANode::enableAdd() {
  m_canAdd = true;
}

void CGRANode::enableMul() {
  m_canMul = true;
}

void CGRANode::enableShift() {
  m_canShift = true;
}

void CGRANode::enablePhi() {
  m_canPhi = true;
}

void CGRANode::enableSel() {
  m_canSel = true;
}

void CGRANode::enableCmp() {
  m_canCmp = true;
}

void CGRANode::enableMAC() {
  m_canMAC = true;
}

void CGRANode::enableLogic() {
  m_canLogic = true;
}

void CGRANode::enableBr() {
  m_canBr = true;
}

bool CGRANode::canCall() {
  return m_canCall;
}

bool CGRANode::canReturn() {
  return m_canReturn;
}

bool CGRANode::canStore() {
  return m_canStore;
}

bool CGRANode::canLoad() {
  return m_canLoad;
}

bool CGRANode::canAdd() {
  return m_canAdd;
}

bool CGRANode::canMul() {
  return m_canMul;
}

bool CGRANode::canShift() {
  return m_canShift;
}

bool CGRANode::canPhi() {
  return m_canPhi;
}

bool CGRANode::canSel() {
  return m_canSel;
}

bool CGRANode::canCmp() {
  return m_canCmp;
}

bool CGRANode::canMAC() {
  return m_canMAC;
}

bool CGRANode::canLogic() {
  return m_canLogic;
}

bool CGRANode::canBr() {
  return m_canBr;
}

int CGRANode::getX() {
  return m_x;
}

int CGRANode::getY() {
  return m_y;
}

void CGRANode::disable() {
  m_disabled = true;
  for (CGRALink* link: m_inLinks) {
    link->disable();
  }
  for (CGRALink* link: m_outLinks) {
    link->disable();
  }
}

void CGRANode::disableAllFUs() {
  m_canReturn = false;
  m_canStore = false;
  m_canLoad = false;
  m_canCall = false;
  m_canAdd = false;
  m_canMul = false;
  m_canShift = false;
  m_canPhi = false;
  m_canSel = false;
  m_canCmp = false;
  m_canMAC = false;
  m_canLogic = false;
  m_canBr = false;
}
