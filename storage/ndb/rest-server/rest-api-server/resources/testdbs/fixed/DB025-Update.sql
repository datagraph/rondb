-- This file is part of the RonDB REST API Server
-- Copyright (c) 2023 Hopsworks AB
--
-- This program is free software: you can redistribute it and/or modify
-- it under the terms of the GNU General Public License as published by
-- the Free Software Foundation, version 3.
--
-- This program is distributed in the hope that it will be useful, but
-- WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
-- General Public License for more details.
--
-- You should have received a copy of the GNU General Public License
-- along with this program. If not, see <http://www.gnu.org/licenses/>.


USE db025;

DROP TABLE table_1;

CREATE TABLE table_1(
    id0 VARCHAR(10),
    new_col0 VARCHAR(100),
    new_col1 VARCHAR(100),
    new_col2 VARCHAR(100),
    PRIMARY KEY(id0)
) ENGINE = ndbcluster;

INSERT INTO
    table_1
VALUES
    (
        '1',
        'col0_data',
        'col1_data',
        'col2_data'
    );
