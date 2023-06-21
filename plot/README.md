# plotcharts
テレメトリーCSVをグラフにする。

# retrieve_plotdata 
M5StickCPlusとWi-SUN HATでスマートメータから情報を得て
DynamoDBに格納されているテレメトリーをCSVファイルにする。


# build
cargo build

or

cargo build --release

# run
cargo run --bin plotcharts 
cargo run --bin retrieve_plotdata 

or

./target/debug/plotcharts [-h, ...]
./target/debug/retrieve_plotdata  [-h, ...]

or

./target/release/plotcharts -h
./target/release/retrieve_plotdata [-h, ...]
