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