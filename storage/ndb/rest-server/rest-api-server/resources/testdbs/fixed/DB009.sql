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

DROP DATABASE IF EXISTS db009;

CREATE DATABASE db009;

USE db009;

CREATE TABLE float_table1(
    id0 INT,
    col0 FLOAT,
    col1 FLOAT UNSIGNED,
    PRIMARY KEY(id0)
) ENGINE = ndbcluster;

INSERT INTO
    float_table1
VALUES
    (1, -123.123, 123.123);

INSERT INTO
    float_table1
VALUES
    (0, 0, 0);

-- NULL values for non primary columns
INSERT INTO
    float_table1
set
    id0 = 2;

CREATE TABLE float_table2(
    id0 FLOAT,
    col0 FLOAT,
    col1 FLOAT UNSIGNED,
    PRIMARY KEY(id0)
) ENGINE = ndbcluster;
