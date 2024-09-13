CREATE TABLE geo_metadata (
	id SERIAL PRIMARY KEY,
	image_md5 VARCHAR(32) UNIQUE NOT NULL,
	timestamp TIMESTAMP NOT NULL,
	latitude DOUBLE PRECISION,
	longitude DOUBLE PRECISION,
	altitude DOUBLE PRECISION,
	sample_geo INT,
	sample_fauna TEXT,
	sample_flora TEXT,
	sample_calls TEXT
);

CREATE TABLE usr_auth (
	id SERIAL PRIMARY KEY,
	uname VARCHAR(255) UNIQUE NOT NULL,
	passhash VARCHAR(255) NOT NULL,
	email VARCHAR(255) NOT NULL,
	apikey VARCHAR(255) UNIQUE,
	last_login TIMESTAMP
);