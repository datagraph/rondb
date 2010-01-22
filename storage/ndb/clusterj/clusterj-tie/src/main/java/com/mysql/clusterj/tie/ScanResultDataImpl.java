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

import java.util.List;

import com.mysql.clusterj.core.store.Column;

import com.mysql.clusterj.core.util.I18NHelper;
import com.mysql.clusterj.core.util.Logger;
import com.mysql.clusterj.core.util.LoggerFactoryService;

import com.mysql.ndbjtie.ndbapi.NdbScanOperation;

/**
 *
 */
class ScanResultDataImpl extends ResultDataImpl {

    /** My message translator */
    static final I18NHelper local = I18NHelper
            .getInstance(ScanResultDataImpl.class);

    /** My logger */
    static final Logger logger = LoggerFactoryService.getFactory()
            .getInstance(ScanResultDataImpl.class);

    private NdbScanOperation ndbScanOperation = null;


    /** Flags for iterating a scan */
    protected final int RESULT_READY = 0;
    protected final int SCAN_FINISHED = 1;
    protected final int CACHE_EMPTY = 2;

    public ScanResultDataImpl(NdbScanOperation ndbScanOperation, List<Column> storeColumns) {
        super(ndbScanOperation, storeColumns);
        this.ndbScanOperation = ndbScanOperation;
    }

    @Override
    public boolean next() {
        // NdbScanOperation may have many results.
        boolean done = false;
        boolean fetch = false;
        boolean force = false; // always false for scans
        while (!done) {
            int result = ndbScanOperation.nextResult(fetch, force);
            switch (result) {
                case RESULT_READY:
                    return true;
                case SCAN_FINISHED:
                    return false;
                case CACHE_EMPTY:
                    fetch = true;
                    break;
                default:
                    Utility.throwError(result, ndbScanOperation.getNdbError());
            }
        }
        return true; // this statement is needed to make the compiler happy but it's never executed
    }

}
