#import <Foundation/Foundation.h>
#import <LocalAuthentication/LocalAuthentication.h>

extern "C" CFTypeRef drawStatsCreateNoninteractiveAuthContext()
{
  LAContext *context = [[LAContext alloc] init];
  context.interactionNotAllowed = YES;
  return CFBridgingRetain(context);
}
