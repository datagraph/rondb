/* 
   Copyright (C) 2003 MySQL AB
    All rights reserved. Use is subject to license terms.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/


#include "LockQueue.hpp"
#include "SimulatedBlock.hpp"

Uint32
LockQueue::lock(SimulatedBlock* block,
                Pool & thePool, 
                const UtilLockReq* req, const UtilLockReq** lockOwner)
{
  const bool exclusive = ! (req->requestInfo & UtilLockReq::SharedLock);
  const bool trylock = req->requestInfo & UtilLockReq::TryLock;
  const bool notify = req->requestInfo & UtilLockReq::Notify;
  
  LocalDLFifoList<LockQueueElement> queue(thePool, m_queue);
  
  bool grant = true;
  Ptr<LockQueueElement> lockEPtr;
  if (queue.last(lockEPtr))
  {
    jamBlock(block);
    if (! (lockEPtr.p->m_req.requestInfo & UtilLockReq::SharedLock))
    {
      jamBlock(block);
      grant = false;
    }
    else if (exclusive)
    {
      jamBlock(block);
      grant = false;
    }
    else if (lockEPtr.p->m_req.requestInfo & UtilLockReq::Granted)
    {
      jamBlock(block);
      grant = true;
    }
    else
    {
      jamBlock(block);
      grant = false;
    }
  }
  
  if(trylock && grant == false)
  {
    jamBlock(block);
    if (notify && lockOwner)
    {
      jamBlock(block);
      queue.first(lockEPtr);
      * lockOwner = &lockEPtr.p->m_req;
    }
    return UtilLockRef::LockAlreadyHeld;
  }
  
  if(!thePool.seize(lockEPtr))
  {
    jamBlock(block);
    return UtilLockRef::OutOfLockRecords;
  }
  
  lockEPtr.p->m_req = *req;
  queue.addLast(lockEPtr);
  
  if(grant)
  {
    jamBlock(block);
    lockEPtr.p->m_req.requestInfo |= UtilLockReq::Granted;
    return UtilLockRef::OK;
  }
  else
  {
    jamBlock(block);
    return UtilLockRef::InLockQueue;
  }
}

Uint32
LockQueue::unlock(SimulatedBlock* block,
                  Pool & thePool, 
                  const UtilUnlockReq* req)
{
  const Uint32 senderRef = req->senderRef;
  const Uint32 senderData = req->senderData;
  
  Ptr<LockQueueElement> lockEPtr;
  LocalDLFifoList<LockQueueElement> queue(thePool, m_queue);
  
  for (queue.first(lockEPtr); !lockEPtr.isNull(); queue.next(lockEPtr))
  {
    jamBlock(block);
    if (lockEPtr.p->m_req.senderData == senderData &&
        lockEPtr.p->m_req.senderRef == senderRef)
    {
      jamBlock(block);
      
      Uint32 res;
      if (lockEPtr.p->m_req.requestInfo & UtilLockReq::Granted)
      {
        jamBlock(block);
        res = UtilUnlockRef::OK;
      }
      else
      {
        jamBlock(block);
        res = UtilUnlockRef::NotLockOwner;
      }
      queue.release(lockEPtr);
      return res;
    }
  }
  
  return UtilUnlockRef::NotInLockQueue;
}

bool
LockQueue::first(SimulatedBlock* block,
                 Pool& thePool, Iterator & iter)
{
  LocalDLFifoList<LockQueueElement> queue(thePool, m_queue);
  if (queue.first(iter.m_curr))
  {
    iter.m_block = block;
    iter.m_prev.setNull();
    iter.thePool = &thePool;
    return true;
  }
  return false;
}

bool
LockQueue::next(Iterator& iter)
{
  iter.m_prev = iter.m_curr;
  LocalDLFifoList<LockQueueElement> queue(*iter.thePool, m_queue);
  return queue.next(iter.m_curr);
}

int
LockQueue::checkLockGrant(Iterator& iter, UtilLockReq* req)
{
  SimulatedBlock* block = iter.m_block;
  LocalDLFifoList<LockQueueElement> queue(*iter.thePool, m_queue);
  if (iter.m_prev.isNull())
  {
    if (iter.m_curr.p->m_req.requestInfo & UtilLockReq::Granted)
    {
      jamBlock(block);
      return 1;
    }
    else
    {
      jamBlock(block);
      * req = iter.m_curr.p->m_req;
      iter.m_curr.p->m_req.requestInfo |= UtilLockReq::Granted;
      return 2;
    }
  }
  else
  {
    jamBlock(block);
    /**
     * Prev is granted...
     */
    assert(iter.m_prev.p->m_req.requestInfo & UtilLockReq::Granted);
    if (iter.m_prev.p->m_req.requestInfo & UtilLockReq::SharedLock)
    {
      jamBlock(block);
      if (iter.m_curr.p->m_req.requestInfo & UtilLockReq::SharedLock)
      {
        jamBlock(block);
        if (iter.m_curr.p->m_req.requestInfo & UtilLockReq::Granted)
        {
          jamBlock(block);
          return 1;
        }
        else
        {
          jamBlock(block);
          * req = iter.m_curr.p->m_req;
          iter.m_curr.p->m_req.requestInfo |= UtilLockReq::Granted;
          return 2;
        }
      }
    }
    return 0;
  }
}

void
LockQueue::clear(Pool& thePool)
{
  LocalDLFifoList<LockQueueElement> queue(thePool, m_queue);
  queue.release();
}

#include "SimulatedBlock.hpp"

void
LockQueue::dump_queue(Pool& thePool, SimulatedBlock* block)
{
  Ptr<LockQueueElement> ptr;
  LocalDLFifoList<LockQueueElement> queue(thePool, m_queue);

  for (queue.first(ptr); !ptr.isNull(); queue.next(ptr))
  {
    jamBlock(block);
    block->infoEvent("- sender: 0x%x data: %u %s %s extra: %u",
                     ptr.p->m_req.senderRef,
                     ptr.p->m_req.senderData,
                     (ptr.p->m_req.requestInfo & UtilLockReq::SharedLock) ? 
                     "S":"X",
                     (ptr.p->m_req.requestInfo & UtilLockReq::Granted) ? 
                     "granted" : "",
                     ptr.p->m_req.extra);
  }
}

