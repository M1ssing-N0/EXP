#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

#include "ParticleFerry.H"
#include "pCell.H"
#include "pHOT.H"
#include "global.H"

int pCell::live = 0;		// Track number of instances

size_t   sCell::CRMsz  = 128;	// Maximum number of cached relative velocities
unsigned pCell::bucket = 7;	// Target microscopic (collision) bucket size
unsigned pCell::Bucket = 64;	// Target macroscopic bucket size
unsigned pCell::deltaL = 2;     // Maximum number of cell expansions to get 
				// sample cell

static unsigned ctargt = 0;

string printKey(key_type p)
{
  const key_type one(1u);
  ostringstream sout, sret;

  unsigned short cnt = 0;
  unsigned Nbits = sizeof(p)*8;
  for (unsigned k=0; k<Nbits; k++) {
#ifdef INT128
    sout << ( (p & one).toUint() ? '1' : '0' );
#else
    sout << ( (p & one) ? '1' : '0' );
#endif
    if (++cnt==3) {sout << '.'; cnt = 0;}
    p = p>>1;
  }

  string s = sout.str();	// Reverse the string
  for (unsigned k=0; k<s.size(); k++) sret << s[s.size()-1-k];

  return sret.str();
}


pCell::pCell(pHOT* tr) : tree(tr), C(tr->cc), isLeaf(true)
{
  live++;

  owner   = myid;
				// I am the root node
  parent  = 0;
  sample  = 0;
  mykey   = 1u;
  level   = 0;
  maxplev = 0;
				// My body mask
  mask    = mykey << 3*(nbits - level);

				// Initialize state
  sKeySet::iterator it;
  for (it=tr->spec_list.begin(); it!=tr->spec_list.end(); it++) {
    count[*it] = 0;
    state[*it] = vector<double>(10, 0.0);
  }

  ctotal  = 0;
  stotal  = vector<double>(10, 0.0);

  
  // Begin with a clean frontier
  tree->frontier.erase(tree->frontier.begin(), tree->frontier.end());

  // Root is born on the frontier and is the only one to start
  tree->frontier[mykey] = this;	

  // DEBUG
  if (this==0) {
    cout << "pCell::pCell: I am null [root]" << endl;
  }
  //
}

pCell::pCell(pCell* mom, unsigned id) : 
  tree(mom->tree), C(mom->C), parent(mom), isLeaf(true)
{
  live++;

  owner   = myid;
				// My map key
  mykey   = (parent->mykey << 3) + id;
				// My level
  level   = parent->level + 1;
				// Maximum particle level
  maxplev = 0;
				// My body mask
  mask    = mykey << 3*(nbits - level);

				// Initialize state
  sKeySet::iterator it;
  for (it=tree->spec_list.begin(); it!=tree->spec_list.end(); it++) {
    count[*it] = 0;
    state[*it] = vector<double>(10, 0.0);
  }

  stotal = vector<double>(10, 0.0);

				// Uninitialized sample cell
  sample  = 0;
  ctotal  = 0;

  tree->frontier[mykey] = this;	// All nodes born on the frontier
  // DEBUG
  if (this==0) {
    cout << "pCell::pCell: I am null [branch/node]" << endl;
  }
  //
}

pCell::~pCell()
{
  live--;

  // Recursively kill all the cells
  for (map<unsigned, pCell*>::iterator it=children.begin(); 
       it!=children.end(); it++) delete it->second;
}

unsigned pCell::childId(key_type key)
{
  key_type id = key - mask;
  id >>= 3*(nbits - 1 - level);
#ifdef INT128
  unsigned cid = id.toUint();
#else
  unsigned cid = static_cast<unsigned>(id);
#endif
  if (cid>7) {
    cout << "Process " << myid << ": crazy cid value: " << cid
	 << " level=" << level << "  id=" << hex << id << dec << endl;
  }
  return cid;
}


pCell* pCell::Add(const key_pair& keypair, change_list* change)
{
  key_type key = keypair.first;
  key_type dif = key - mask;
  unsigned key2;

				// Check that this key belongs to this branch
  key_type sig = dif >> 3*(nbits-level);

				// Cell key, particle index pair
  key_pair cellindx(mykey, keypair.second);

				// Wrong branch!
  if (!!sig) {
				// Sanity check . . . if this is the root,
				// we might be in the wrong tree (a bug)
    if (parent == 0) {
      cout << "Process " << myid << ": ERROR level=" << level << endl << hex
	   << "  key=" << key   << endl
	   << " mask=" << mask  << endl
	   << " diff=" << dif   << endl
	   << "  sig=" << sig   << endl
	   << " xsig=" << ( (key - mask) >> 3*(nbits-level) ) << endl << dec
	   << " shft=" << 3*(nbits-level)   << endl;

				// Get the particle info
      key_indx::iterator p =
	lower_bound(tree->keybods.begin(), tree->keybods.end(), keypair, ltPAIR());

      while (p->first==keypair.first && p->second==keypair.second ) {
	cout << "pos=";
	for (int k=0; k<3; k++) 
	  cout << setw(18) << C->Particles()[p->second].pos[k];
	cout << endl;
	p++;
      }
    }
				// Move up the tree . . .
    return parent->Add(keypair, change);
  }
  
				// If this cell is a leaf, try to add the
				// new body
  if (isLeaf && keys.find(keypair)==keys.end()) {

				// I am still a leaf . . .
    if (bods.size() < bucket || level+1==nbits) {
      keys.insert(keypair);
      tree->bodycell.insert(key_item(key, cellindx));
      bods.push_back(keypair.second);
				// Flag to recompute sample cell
      if (change) change->push_back(cell_indx(this, pHOT::RECOMP));
      maxplev = max<int>(maxplev, C->Particles()[keypair.second].level);
      
      return this;
    }
    
				// Bucket is full, I need to make
				// leaves and become a branch
    for (key_indx::iterator n=keys.begin(); n!=keys.end(); n++) {
				// Give all of my body keys to my
				// children
      key2 = childId(n->first);
      if (children.find(key2) == children.end()) {
	children[key2] = new pCell(this, key2);
	if (change) change->push_back(cell_indx(children[key2], pHOT::CREATE));
      }
      
      key2Range ik = tree->bodycell.equal_range(n->first);
      key2Itr jk = ik.first, rmv;
      while ((rmv=jk++)!=ik.second) {
	if (rmv->second.second == n->second)
	  tree->bodycell.erase(rmv);
      }

      children[key2]->Add(*n, change);
    }
				// Erase my list
    keys.clear();
    bods.clear();
				// Erase my cell key from the frontier
    tree->frontier.erase(mykey);
    if (change) change->push_back(cell_indx(this, pHOT::REMOVE));
				// I'm a branch now . . .
    isLeaf = false;
  }

				// Now add the *new* key
  key2 = childId(key);
  if (children.find(key2) == children.end()) {
    children[key2] = new pCell(this, key2);
    if (change) change->push_back(cell_indx(children[key2], pHOT::CREATE));
  }

  
  return children[key2]->Add(keypair, change);
}
  

void pCell::RemoveKey(const key_pair& pr)
{
				// Erase pair from my keys list
  key_indx::iterator it=keys.find(pr);
  if (it != keys.end()) keys.erase(it);

#ifdef DEBUG
  else {
    //-----------------------------------------------------------------      
    cout << "Process " << myid << ": cell=" 
#ifdef INT128
	 << mykey.toHex() 
#else
	 << hex << mykey << dec
#endif
	 << " key not in keys list!" << endl;
    //-----------------------------------------------------------------      
  }
#endif
				// Erase the body from this tree's
				// cell list
  key2Range ik = tree->bodycell.equal_range(pr.first);
  key2Itr   ij = ik.first, rmv;

  while (ij!=ik.second) {
    if ((rmv=ij++)->second.second == pr.second) {
      tree->bodycell.erase(rmv);
    }
  }

#ifdef DEBUG
  else {
    //-----------------------------------------------------------------      
    cout << "Process " << myid << ": cell=" 
#ifdef INT128
	 << mykey.toHex() 
#else
	 << hex << mykey << dec
#endif
	 << " key not in cell list!" << endl;
    //-----------------------------------------------------------------      
  }
#endif

				// Erase the key from the tree's
				// key-body index
  key_indx::iterator ip = tree->keybods.find(pr);
  if (ip != tree->keybods.end()) tree->keybods.erase(ip);

#ifdef DEBUG
  else {
    //-----------------------------------------------------------------      
    cout << "Process " << myid << ": cell=" 
#ifdef INT128
	 << mykey.toHex()
#else
	 << hex << mykey << dec
#endif
	 << " missing keypair entry in keybods,";
    cout << "key=" 
#ifdef INT128
	 << pr.first.toHex() 
#else
	 << hex << pr.first << dec
#endif
	 << " index=" << pr.second
	 << endl;
    //-----------------------------------------------------------------      
  }
#endif

#ifdef ADJUST_INFO
  //-----------------------------------------------------------------      
  cout << "Process " << myid << ": pCell::REMOVED KEY=" 
#ifdef INT128
       << pr.first.toHex()
#else
       << hex << pr.first << dec
#endif
       << " index=" << pr.second << endl;
  //-----------------------------------------------------------------      
#endif
}

void pCell::UpdateKeys(const key_pair& oldpair, const key_pair& newpair)
{
  RemoveKey(oldpair);
  keys.insert(newpair);
  tree->keybods.insert(newpair);
  tree->bodycell.insert(key_item(newpair.first, 
				 key_pair(mykey, newpair.second)));
#ifdef ADJUST_INFO
  //-----------------------------------------------------------------      
  cout << "Process " << myid << ": "
       << "pCell::INSERTED KEY=" << newpair.first.toHex()
       << " index=" << newpair.second << endl;
  //-----------------------------------------------------------------      
#endif
}

bool pCell::Remove(const key_pair& keypair, change_list* change)
{
  bool ret = false;
				// Sanity check: is this really my
				// key?
  if (isMine(keypair.first)) {
				// Remove keypair from cell list
#ifdef DEBUG
    //-----------------------------------------------------------------      
    if (keys.find(keypair) == keys.end()) {
      cout << "Process " << myid << ": "
	   << "pCell::Remove: ERROR finding keypair in cell's list" 
#ifdef INT128
	   << " cur=" << mykey.toHex()
	   << " key=" << keypair.first.toHex() 
#else
	   << hex << " cur=" << mykey
	   << " key=" << keypair.first << dec
#endif
	   << " index=" << keypair.second
	   << endl;
      return ret;
    }
    //-----------------------------------------------------------------      
#endif
    keys.erase(keypair);
				// Remove from body/cell key list
    key2Itr ik = tree->bodycell.find(keypair.first);
#ifdef DEBUG
    //-----------------------------------------------------------------      
    if (ik == tree->bodycell.end()) {
      cout << "Process " << myid << ": "
	   << "pCell::Remove: ERROR finding key in bodycell"
#ifdef INT128
	   << " key="   << keypair.first.toHex() 
#else
	   << " key="   << hex << keypair.first << dec
#endif
	   << " index=" << keypair.second
	   << endl;
      return ret;
    }
    //-----------------------------------------------------------------      
#endif

				// Remove the key-body entry
    key_indx::iterator p = tree->keybods.find(keypair);
#ifdef DEBUG
    //-----------------------------------------------------------------      
    if (p==tree->keybods.end()) {
      cout << "Process " << myid << ": "
	   << "pCell::Remove: ERROR missing keypair entry in keybods," 
#ifdef INT128
	   << " key="   << keypair.first.toHex() 
#else
	   << " key="   << hex << keypair.first << dec
#endif
	   << " index=" << keypair.second
	   << endl;
      return ret;
    }
    //-----------------------------------------------------------------      
#endif
    if (p!=tree->keybods.end()) tree->keybods.erase(p);
    
    // Remove the index from the cell body list
    //
    vector<unsigned long>::iterator ib = find(bods.begin(), bods.end(), 
					      keypair.second);
    if (ib!=bods.end()) bods.erase(ib);
#ifdef DEBUG
    else {
    //-----------------------------------------------------------------      
      cout << "Process " << myid << ": "
	   << "pCell::Remove: ERROR missing index in bods,"
#ifdef INT128
	   << " key="   << keypair.first.toHex() 
#else
	   << " key="   << hex << keypair.first << dec
#endif
	   << " index=" << keypair.second
	   << endl;
      return ret;
    //-----------------------------------------------------------------      
    }
#endif

    // Remove this cell if it is now empty (and not the root node)
    //
    if (bods.empty() && parent!=NULL) {

      // Find the parent delete the cell from the parent list
      //
      bool found = false;
      for (map<unsigned, pCell*>::iterator ic=parent->children.begin(); 
	   ic!=parent->children.end(); ic++) {
	if (ic->second == this) {
	  parent->children.erase(ic);
	  found = true;
	  break;
	}
      }
      if (!found) {
	cout << "Process " << myid 
	     << ": pCell::Remove: ERROR child not found on parent's list!" 
	     << endl;
      }

      // Remove me from the frontier
      //
      tree->frontier.erase(mykey);

      // Remove the old pair from the current cell
      // (only transactions added are sample cells)
      // queue for removal from level lists
      //
      change->push_back(cell_indx(this, pHOT::REMOVE));
      
      // queue for deletion
      //
      change->push_back(cell_indx(this, pHOT::DELETE));
    
      ret = true;
    }
    else change->push_back(cell_indx(this, pHOT::RECOMP));
    
  } else {
    cout << "Process " << myid 
	 << ": pCell::Remove: ERROR body not in my cell"
	 << ", cell key=" << hex << mykey
	 << ", body key=" << keypair.first
	 << ", sig=" 
	 << ((key_type)(keypair.first - mask) >> 3*(nbits-level)) << dec
	 << " body index=" << keypair.second << endl;
  }

  return ret;
}


void pCell::RemoveAll()
{
  key_indx::iterator k;
  key_key ::iterator ik;
  key_indx::iterator p;

  while (keys.size()) {
    k = keys.begin();
    ik = tree->bodycell.find(k->first);
    if (ik != tree->bodycell.end()) {
      tree->bodycell.erase(ik);
    }
    p = tree->keybods.find(*k);
    if (p!=tree->keybods.end()) {
      tree->keybods.erase(p);
    }
    keys.erase(k);
  }

  bods.clear();
  if (mykey!=1u) tree->frontier.erase(mykey);

  if (parent) {
    for (map<unsigned, pCell*>::iterator ic=parent->children.begin(); 
	 ic!=parent->children.end(); ic++) {
      if (ic->second == this) {
	parent->children.erase(ic);
	return;
      }
    }

    cout << "Process " << myid 
	 << ": pCell::RemoveAll: "
	 << "ERROR child not found on parent's list!" << endl;
    
  } else {

    if (mykey!=1u) {
      cout << "Process "  << myid  << ": ERROR no parent and not root!"
	   << " owner="   << owner << hex
	   << " mykey="   << mykey
	   << " mask="    << mask  << dec
	   << " level="   << level    
	   << " count="   << ctotal
	   << " maxplev=" << maxplev << endl;
      
    }
  }

  maxplev = 0;
  ctotal  = 0;
}

pCell* pCell::findNode(const key_type& key)
{
				// Check that this key belongs to this branch
  key_type sig = (key_type)(key - mask) >> 3*(nbits-level);
  
  if (!!sig) {
    
    if (parent == 0) {
      cout << "pHOT::findNode: impossible condition, process " 
	   << myid << ": level=" << level  << hex
	   << " key=" << key << endl
	   << " sig=" << sig << endl << dec;
    }

    return parent->findNode(key);
  }

				// You found me!
  if (isLeaf) return this;
				// Which child
  unsigned key2 = childId(key);
				// Not in my tree?
  if (children.find(key2)==children.end()) return 0;

				// Look for node amongst children
  return children[key2]->findNode(key);
}
 
void pCell::zeroState()
{
  sKeySet::iterator it;
  for (it=tree->spec_list.begin(); it!=tree->spec_list.end(); it++) {
    count[*it] = 0;
    for (int k=0; k<10; k++) state[*it][k] = 0.0;
  }

  for (map<unsigned, pCell*>::iterator it = children.begin();
       it != children.end(); it++) it->second->zeroState();

  ctotal = 0;
  for (int k=0; k<10; k++) stotal[k] = 0.0;
}


void pCell::accumState()
{
				// March through the body list
  vector<unsigned long>::iterator j;
  for (j=bods.begin(); j!=bods.end(); j++) {
    speciesKey spc(C->Particles()[*j].Z, C->Particles()[*j].C);
    state[spc][0] += C->Particles()[*j].mass;
    for (int k=0; k<3; k++) {
      state[spc][1+k] += C->Particles()[*j].mass * 
	C->Particles()[*j].vel[k]*C->Particles()[*j].vel[k];
      state[spc][4+k] += C->Particles()[*j].mass * C->Particles()[*j].vel[k];
      state[spc][7+k] += C->Particles()[*j].mass * C->Particles()[*j].pos[k];
    }
    count[spc]++;
  }
  
  sKeySet::iterator it;
  for (it=tree->spec_list.begin(); it!=tree->spec_list.end(); it++) {
    ctotal += count[*it];
    for (int k=0; k<10; k++) stotal[k] += state[*it][k];
  }
    
				// Walk up the tree . . .
  if (parent) parent->accumState(count, state);
}

void pCell::accumState(sKeyUmap& _count, sKeyvDmap& _state)
{
#pragma omp critical
  {
    sKeySet::iterator it;
    for (it=tree->spec_list.begin(); it!=tree->spec_list.end(); it++) {
      ctotal     += _count[*it];
      count[*it] += _count[*it];
      for (int k=0; k<10; k++) {
	stotal[k]     += _state[*it][k];
	state[*it][k] += _state[*it][k];
      }
    }
  }

  if (parent) parent->accumState(_count, _state);
}


void pCell::Find(key_type key, unsigned& curcnt, unsigned& lev,
		 vector<double>& st)
{
  if (key==0u) {
    curcnt = 0;
    lev    = 0;
    for (vector<double>::iterator s=st.begin(); s!=st.end(); s++) *s = 0;
    return;
  }
    

  // Check to see if this key belongs to one of the children
  //
  key_type cid = key - mask;
  cid = cid >> 3*(nbits - 1 - level);

  for( map<unsigned, pCell*>::iterator it = children.begin();
       it != children.end(); it++) {
    if (cid == it->first) {
      it->second->Find(key, curcnt, lev, st);
      return;
    }
  }

  // Return the values from this cell
  //
  curcnt = ctotal;
  lev    = level;
  st     = stotal;

  return;
}


void pCell::Find(key_type key, sKeyUmap& curcnt, unsigned& lev, sKeyvDmap& st)
{
  sKeySet::iterator it;

  if (key==0u) {
    lev = 0;

    for (it=tree->spec_list.begin(); it!=tree->spec_list.end(); it++) {
      curcnt[*it] = 0;
      for (vector<double>::iterator 
	     s=st[*it].begin(); s!=st[*it].end(); s++) *s = 0;
    }

    return;
  }
    

  // Check to see if this key belongs to one of the children
  //
  key_type cid = key - mask;
  cid = cid >> 3*(nbits - 1 - level);

  for (map<unsigned, pCell*>::iterator it = children.begin();
       it != children.end(); it++) {
    if (cid == it->first) {
      it->second->Find(key, curcnt, lev, st);
      return;
    }
  }

  // Return the values from this cell
  //
  curcnt = count;
  lev    = level;
  st     = state;

  return;
}

double sCell::Mass()
{
  double mass = 0.0;
  sKeyvDmap::iterator it;
  for (it=state.begin(); it!=state.end(); it++) mass += (it->second)[0];
  return mass; 
}

double sCell::Mass(speciesKey indx)
{
  sKeyvDmap::iterator it = state.find(indx);
  if (it != state.end()) return (it->second)[0];
  else                   return 0.0;
}

unsigned sCell::Count()
{
  double number = 0.0;
  for (sKeyvDmap::iterator it=state.begin(); it!=state.end(); it++) 
    number += (it->second)[10];
  return number; 
}

unsigned sCell::Count(speciesKey indx)
{
  sKeyUmap::iterator it = count.find(indx);
  if (it != count.end()) return it->second;
  else                   return 0;
}

void sCell::MeanPos(speciesKey indx, double &x, double &y, double& z)
{
  sKeyvDmap::iterator it = state.find(indx);

  if (it==state.end()) {
    x = y = z = 0.0;
    return;
  }

  if ((it->second)[0]<=0.0) {
    x = y = z = 0.0;
    return;
  }

  x = (it->second)[7]/(it->second)[0];
  y = (it->second)[8]/(it->second)[0];
  z = (it->second)[9]/(it->second)[0];
}

void sCell::MeanPos(speciesKey indx, vector<double> &p)
{
  p = vector<double>(3, 0);
  sKeyvDmap::iterator it = state.find(indx);
  if (it == state.end())    return;
  if ((it->second)[0]<=0.0) return;
  for (int k=0; k<3; k++) p[k] = (it->second)[7+k]/(it->second)[0];
}

void sCell::MeanVel(speciesKey indx, double &u, double &v, double& w)
{
  sKeyvDmap::iterator it = state.find(indx);

  if (it == state.end()) {
    u = v = w = 0.0;
    return;
  }

  if ((it->second)[0]<=0.0) {
    u = v = w = 0.0;
    return;
  }
  u = (it->second)[4]/(it->second)[0];
  v = (it->second)[5]/(it->second)[0];
  w = (it->second)[6]/(it->second)[0];
}

void sCell::MeanVel(speciesKey indx, vector<double> &p)
{
  p = vector<double>(3, 0);

  sKeyvDmap::iterator it = state.find(indx);
  if (it == state.end())    return;
  if ((it->second)[0]<=0.0) return;

  for (int k=0; k<3; k++) p[k] = (it->second)[4+k]/(it->second)[0];
}
void sCell::MeanPos(double &x, double &y, double& z)
{
  if (stotal[0]<=0.0) {
    x = y = z = 0.0;
    return;
  }
  x = stotal[7]/stotal[0];
  y = stotal[8]/stotal[0];
  z = stotal[9]/stotal[0];
}

void sCell::MeanPos(vector<double> &p)
{
  p = vector<double>(3, 0);
  if (stotal[0]<=0.0) return;
  for (int k=0; k<3; k++) p[k] = stotal[7+k]/stotal[0];
}

void sCell::MeanVel(double &u, double &v, double& w)
{
  if (stotal[0]<=0.0) {
    u = v = w = 0.0;
    return;
  }
  u = stotal[4]/stotal[0];
  v = stotal[5]/stotal[0];
  w = stotal[6]/stotal[0];
}

void sCell::MeanVel(vector<double> &p)
{
  p = vector<double>(3, 0);
  if (stotal[0]<=0.0) return;
  for (int k=0; k<3; k++) p[k] = stotal[4+k]/stotal[0];
}

void sCell::KE(speciesKey indx, double &total, double &dispr)
{
  total = 0.0;
  dispr = 0.0;

  sKeyvDmap::iterator it = state.find(indx);
  if (it == state.end()) return;

  if ((it->second)[0]>0.0) {
    for (int k=0; k<3; k++) {
      total += 0.5*(it->second)[1+k];
      dispr += 0.5*((it->second)[1+k] - (it->second)[4+k]*(it->second)[4+k]/(it->second)[0]);
    }

    if (count[indx]<2) dispr=0.0;

#ifdef DEBUG
    //-----------------------------------------------------------------      
    static int cnt = 0;
    if (dispr<0.0) {
      ostringstream sout;
      sout << "pCell_tst." << myid << "." << cnt++;
      ofstream out(sout.str().c_str());
      out << "# number=" << count << ", index=" << indx << endl;
      for (unsigned i=0; i<10; i++) 
	out << setw(3) << i << setw(15) << (it->second)[i] << endl;
    }
    //-----------------------------------------------------------------      
#endif

    dispr = max<double>(0.0, dispr);
    
    // Return energy per unit mass
    //
    total /= (it->second)[0];
    dispr /= (it->second)[0];

    //if(dispr == 0) { cout << "ctotal = " << count[indx] << " stotal[0] = " << it->second[0] << " Z = " << indx.first << " C = " << indx.second << endl; }
  }

}

void sCell::KE(double &total, double &dispr)
{
  total = 0.0;
  dispr = 0.0;

  if (stotal[0]>0.0) {
    for (int k=0; k<3; k++) {
      total += 0.5*stotal[1+k];
      dispr += 0.5*(stotal[1+k] - stotal[4+k]*stotal[4+k]/stotal[0]);
    }

    if (ctotal<2) dispr=0.0;

#ifdef DEBUG
    //-----------------------------------------------------------------      
    static int cnt = 0;
    if (dispr<0.0) {
      ostringstream sout;
      sout << "pCell_tst." << myid << "." << cnt++;
      ofstream out(sout.str().c_str());
      out << "# number=" << count << endl;
      for (unsigned i=0; i<10; i++) 
	out << setw(3) << i << setw(15) << stotal[i] << endl;
    }
    //-----------------------------------------------------------------      
#endif

    dispr = max<double>(0.0, dispr);
    
    // Return energy per unit mass
    //
    total /= stotal[0];
    dispr /= stotal[0];
  }

}

void pCell::Vel(double &mass, vector<double>& v1, vector<double>& v2)
{
  mass = 0.0;
  v1 = vector<double>(3, 0.0);
  v2 = vector<double>(3, 0.0);

  if (isLeaf) {
    for (vector<unsigned long>::iterator 
	   i=bods.begin(); i!=bods.end(); i++) {
      for (int k=0; k<3; k++) {
	v1[k] += C->Particles()[*i].mass * 
	  C->Particles()[*i].vel[k];

	v2[k] += C->Particles()[*i].mass * 
	  C->Particles()[*i].vel[k] * C->Particles()[*i].vel[k];
      }
      mass += C->Particles()[*i].mass;
    }
  }

}

double pCell::Volume()
{
#ifdef INT128
  return tree->volume/(key_type(1u) << 3*level).toDouble();
#else
  return tree->volume/static_cast<double>(key_type(1u) << 3*level);
#endif
}

double pCell::Scale()
{
#ifdef INT128
  return 1.0/(key_type(1u) << level).toDouble();
#else
  return 1.0/static_cast<double>(key_type(1u) << level);
#endif
}

sCell* pCell::findSampleCell()
{
  pCell *cur   = this;		// Begin with this cell
  unsigned dbl = 0;		// Count the number of levels upwards
  while(cur->ctotal < Bucket) {
				// Maximum expansion reached or we are
				// at the root
    if (cur->parent==0 || dbl==deltaL) break;
				
    cur = cur->parent;		// Keep walking up the tree . . .
    dbl++;
  }

  sample = cur;			// The answer.

  return sample;
}


Particle* pCell::Body(vector<unsigned long>::iterator k)
{ 
  if (k==bods.end()) return 0;
  return &(C->Particles()[*k]); 
}

unsigned pCell::remake_plev()
{
  maxplev = 0;
  for (vector<unsigned long>::iterator 
	 i=bods.begin(); i!=bods.end(); i++) {
    maxplev = max<unsigned>(maxplev, C->Particles()[*i].level);
  }
  maxplev = min<unsigned>(maxplev, multistep);
  return maxplev;
}

double sCell::CRMavg()
{
  if (CRMlist.size()==0 || CRMnum<=0) return -1.0;
  return CRMsum/CRMnum;
}

double sCell::CRMavg(speciesKey indx)
{
  std::map<speciesKey, deque<double> >::iterator it = CRMlistM.find(indx);
  
  if (it == CRMlistM.end()) return -1.0;

  if (it->second.size()==0 || CRMnumM[indx]<=0) return -1.0;

  return CRMsumM[indx]/CRMnumM[indx];
}

void sCell::CRMadd(speciesKey indx, double crm)
{
  unsigned sz = 0;

  if (CRMlistM.find(indx) != CRMlistM.end()) sz = CRMlistM.size();

  // Initialize running average
  //
  if (sz==0) {
    CRMsumM[indx] = 0.0;
    CRMnumM[indx] = 0;
  }

  // Add new element
  //
  CRMlistM[indx].push_back(crm);
  CRMsumM[indx] += crm;
  CRMnumM[indx] += 1;

  // Push out the oldest element
  // if deque is full
  //
  if (sz==CRMsz) {
    CRMsumM[indx] -= CRMlistM[indx].front();
    CRMnumM[indx] -= 1;
    CRMlistM[indx].pop_front();
  }
}


void sCell::CRMadd(double crm)
{
  unsigned sz = CRMlist.size();

  // Initialize running average
  //
  if (sz==0) {
    CRMsum = 0.0;
    CRMnum = 0;
  }

  // Add new element
  //
  CRMlist.push_back(crm);
  CRMsum += crm;
  CRMnum += 1;

  // Push out the oldest element
  // if deque is full
  //
  if (sz==CRMnum) {
    CRMsum -= CRMlist.front();
    CRMlist.pop_front();
    CRMnum -= 1;
  }
}