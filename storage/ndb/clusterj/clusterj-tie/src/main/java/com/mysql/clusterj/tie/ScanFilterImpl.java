/*
 *  Copyright (C) 2009 Sun Microsystems, Inc.
 *  All rights reserved. Use is subject to license terms.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
 */

package com.mysql.clusterj.tie;

import java.math.BigDecimal;
import java.math.BigInteger;
import java.nio.ByteBuffer;
import java.util.Arrays;

import com.mysql.ndbjtie.ndbapi.NdbScanFilter;

import com.mysql.clusterj.ClusterJFatalInternalException;

import com.mysql.clusterj.core.store.Column;
import com.mysql.clusterj.core.store.ScanFilter;
import com.mysql.clusterj.core.util.I18NHelper;
import com.mysql.clusterj.core.util.Logger;
import com.mysql.clusterj.core.util.LoggerFactoryService;

/**
 *
 */
class ScanFilterImpl implements ScanFilter {

    /** My message translator */
    static final I18NHelper local = I18NHelper
            .getInstance(ScanFilterImpl.class);

    /** My logger */
    static final Logger logger = LoggerFactoryService.getFactory()
            .getInstance(ScanFilterImpl.class);

    private NdbScanFilter ndbScanFilter;

    public ScanFilterImpl(NdbScanFilter ndbScanFilter) {
        this.ndbScanFilter = ndbScanFilter;
    }

    public void begin() {
        int returnCode = ndbScanFilter.begin(NdbScanFilter.Group.AND);
        handleError(returnCode, ndbScanFilter);
    }

    public void begin(Group group) {
        int returnCode = ndbScanFilter.begin(convertGroup(group));
        handleError(returnCode, ndbScanFilter);
    }

    public void cmpBigInteger(BinaryCondition condition, Column storeColumn, BigInteger value) {
        ByteBuffer buffer = Utility.convertValue(storeColumn, value);
        int returnCode = ndbScanFilter.cmp(convertCondition(condition),
                storeColumn.getColumnId(), buffer, buffer.capacity());
        handleError(returnCode, ndbScanFilter);
    }

    public void cmpBoolean(BinaryCondition condition, Column storeColumn, boolean value) {
        byte byteValue = (value?(byte)0x01:(byte)0x00);
        cmpByte(condition, storeColumn, byteValue);
    }

    public void cmpByte(BinaryCondition condition, Column storeColumn, byte value) {
        ByteBuffer buffer = Utility.convertValue(storeColumn, value);
        int returnCode = ndbScanFilter.cmp(convertCondition(condition),
                storeColumn.getColumnId(), buffer, buffer.capacity());
        handleError(returnCode, ndbScanFilter);
    }

    public void cmpBytes(BinaryCondition condition, Column storeColumn, byte[] value) {
        ByteBuffer buffer = Utility.convertValue(storeColumn, value);
        int returnCode = ndbScanFilter.cmp(convertCondition(condition),
                storeColumn.getColumnId(), buffer, buffer.capacity());
        handleError(returnCode, ndbScanFilter);
    }

    public void cmpDecimal(BinaryCondition condition, Column storeColumn, BigDecimal value) {
        ByteBuffer buffer = Utility.convertValue(storeColumn, value);
        int returnCode = ndbScanFilter.cmp(convertCondition(condition),
                storeColumn.getColumnId(), buffer, buffer.capacity());
        handleError(returnCode, ndbScanFilter);
    }

    public void cmpDouble(BinaryCondition condition, Column storeColumn, double value) {
        ByteBuffer buffer = Utility.convertValue(storeColumn, value);
        int returnCode = ndbScanFilter.cmp(convertCondition(condition),
                storeColumn.getColumnId(), buffer, buffer.capacity());
        handleError(returnCode, ndbScanFilter);
    }

    public void cmpFloat(BinaryCondition condition, Column storeColumn, float value) {
        ByteBuffer buffer = Utility.convertValue(storeColumn, value);
        int returnCode = ndbScanFilter.cmp(convertCondition(condition),
                storeColumn.getColumnId(), buffer, buffer.capacity());
        handleError(returnCode, ndbScanFilter);
    }

    public void cmpShort(BinaryCondition condition, Column storeColumn, short value) {
        ByteBuffer buffer = Utility.convertValue(storeColumn, value);
        int returnCode = ndbScanFilter.cmp(convertCondition(condition),
                storeColumn.getColumnId(), buffer, buffer.capacity());
        handleError(returnCode, ndbScanFilter);
    }

    public void cmpInt(BinaryCondition condition, Column storeColumn, int value) {
        ByteBuffer buffer = Utility.convertValue(storeColumn, value);
        int returnCode = ndbScanFilter.cmp(convertCondition(condition),
                storeColumn.getColumnId(), buffer, buffer.capacity());
        handleError(returnCode, ndbScanFilter);
    }

    public void cmpLong(BinaryCondition condition, Column storeColumn, long value) {
        ByteBuffer buffer = Utility.convertValue(storeColumn, value);
        if (logger.isDetailEnabled()) {
            int bufferLength = buffer.limit() - buffer.position();
            byte[] array = new byte[bufferLength];
            buffer.get(array);
            buffer.flip();
            logger.detail("column: " + storeColumn.getName() + " condition: " + condition.toString() + " value: " + value + Arrays.toString(array) + "(" + buffer.capacity() + ")");
        }
        int returnCode = ndbScanFilter.cmp(convertCondition(condition),
                storeColumn.getColumnId(), buffer, buffer.capacity());
        handleError(returnCode, ndbScanFilter);
    }

    public void cmpString(BinaryCondition condition, Column storeColumn, String value) {
        ByteBuffer buffer = Utility.convertValue(storeColumn, value);
        int returnCode = ndbScanFilter.cmp(convertCondition(condition),
                storeColumn.getColumnId(), buffer, buffer.capacity());
        handleError(returnCode, ndbScanFilter);
    }

    public void end() {
        int returnCode = ndbScanFilter.end();
        handleError(returnCode, ndbScanFilter);
    }

    private int convertCondition(BinaryCondition condition) {
        switch (condition) {
            case COND_EQ:
                return NdbScanFilter.BinaryCondition.COND_EQ;
            case COND_LE:
                return NdbScanFilter.BinaryCondition.COND_LE;
            case COND_LT:
                return NdbScanFilter.BinaryCondition.COND_LT;
            case COND_GE:
                return NdbScanFilter.BinaryCondition.COND_GE;
            case COND_GT:
                return NdbScanFilter.BinaryCondition.COND_GT;
            default:
                throw new ClusterJFatalInternalException(
                        local.message("ERR_Implementation_Should_Not_Occur"));
        }
    }

    private int convertGroup(Group group) {
        switch(group) {
            case GROUP_AND:
                return NdbScanFilter.Group.AND;
            default:
                throw new ClusterJFatalInternalException(
                        local.message("ERR_Implementation_Should_Not_Occur"));
        }
    }

    protected static void handleError(int returnCode, NdbScanFilter ndbScanFilter) {
        if (returnCode == 0) {
            return;
        } else {
            Utility.throwError(returnCode, ndbScanFilter.getNdbError());
        }
    }

    protected static void handleError(Object object, NdbScanFilter ndbScanFilter) {
        if (object != null) {
            return;
        } else {
            Utility.throwError(null, ndbScanFilter.getNdbError());
        }
    }

}
