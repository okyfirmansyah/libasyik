For logging, Libasyik is equipped with simple logging framework based on [Aixlog](https://github.com/badaix/aixlog).

For example test.cpp:
```c++
#include "aixlog.hpp"
#include "libasyik/service.hpp"

int main()
{
  auto as = asyik::make_service();

  LOG(DEBUG)<<"This is INFO log\n";
  LOG(INFO)<<"This is INFO log\n";
  LOG(WARNING)<<"This is WARNING log\n";
  LOG(ERROR)<<"This is ERROR log\n";

  as->run();
}
```

will output:
```
root@desktop:/workspaces/test/build# ./test 
2020-06-21 04-33-50.930 [Debug] (main) This is INFO log
2020-06-21 04-33-50.930 [Info] (main) This is INFO log
2020-06-21 04-33-50.930 [Warn] (main) This is WARNING log
2020-06-21 04-33-50.930 [Error] (main) This is ERROR log
```
