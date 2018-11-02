// Code generated by MockGen. DO NOT EDIT.
// Source: auth.pb.go

// Package mock_auth is a generated GoMock package.
package mock_auth

import (
	gomock "github.com/golang/mock/gomock"
	context "golang.org/x/net/context"
	grpc "google.golang.org/grpc"
	proto "pixielabs.ai/pixielabs/services/auth/proto"
	reflect "reflect"
)

// MockAuthServiceClient is a mock of AuthServiceClient interface
type MockAuthServiceClient struct {
	ctrl     *gomock.Controller
	recorder *MockAuthServiceClientMockRecorder
}

// MockAuthServiceClientMockRecorder is the mock recorder for MockAuthServiceClient
type MockAuthServiceClientMockRecorder struct {
	mock *MockAuthServiceClient
}

// NewMockAuthServiceClient creates a new mock instance
func NewMockAuthServiceClient(ctrl *gomock.Controller) *MockAuthServiceClient {
	mock := &MockAuthServiceClient{ctrl: ctrl}
	mock.recorder = &MockAuthServiceClientMockRecorder{mock}
	return mock
}

// EXPECT returns an object that allows the caller to indicate expected use
func (m *MockAuthServiceClient) EXPECT() *MockAuthServiceClientMockRecorder {
	return m.recorder
}

// Login mocks base method
func (m *MockAuthServiceClient) Login(ctx context.Context, in *proto.LoginRequest, opts ...grpc.CallOption) (*proto.LoginReply, error) {
	varargs := []interface{}{ctx, in}
	for _, a := range opts {
		varargs = append(varargs, a)
	}
	ret := m.ctrl.Call(m, "Login", varargs...)
	ret0, _ := ret[0].(*proto.LoginReply)
	ret1, _ := ret[1].(error)
	return ret0, ret1
}

// Login indicates an expected call of Login
func (mr *MockAuthServiceClientMockRecorder) Login(ctx, in interface{}, opts ...interface{}) *gomock.Call {
	varargs := append([]interface{}{ctx, in}, opts...)
	return mr.mock.ctrl.RecordCallWithMethodType(mr.mock, "Login", reflect.TypeOf((*MockAuthServiceClient)(nil).Login), varargs...)
}

// GetAugmentedToken mocks base method
func (m *MockAuthServiceClient) GetAugmentedToken(ctx context.Context, in *proto.GetAugmentedAuthTokenRequest, opts ...grpc.CallOption) (*proto.GetAugmentedAuthTokenResponse, error) {
	varargs := []interface{}{ctx, in}
	for _, a := range opts {
		varargs = append(varargs, a)
	}
	ret := m.ctrl.Call(m, "GetAugmentedToken", varargs...)
	ret0, _ := ret[0].(*proto.GetAugmentedAuthTokenResponse)
	ret1, _ := ret[1].(error)
	return ret0, ret1
}

// GetAugmentedToken indicates an expected call of GetAugmentedToken
func (mr *MockAuthServiceClientMockRecorder) GetAugmentedToken(ctx, in interface{}, opts ...interface{}) *gomock.Call {
	varargs := append([]interface{}{ctx, in}, opts...)
	return mr.mock.ctrl.RecordCallWithMethodType(mr.mock, "GetAugmentedToken", reflect.TypeOf((*MockAuthServiceClient)(nil).GetAugmentedToken), varargs...)
}

// MockAuthServiceServer is a mock of AuthServiceServer interface
type MockAuthServiceServer struct {
	ctrl     *gomock.Controller
	recorder *MockAuthServiceServerMockRecorder
}

// MockAuthServiceServerMockRecorder is the mock recorder for MockAuthServiceServer
type MockAuthServiceServerMockRecorder struct {
	mock *MockAuthServiceServer
}

// NewMockAuthServiceServer creates a new mock instance
func NewMockAuthServiceServer(ctrl *gomock.Controller) *MockAuthServiceServer {
	mock := &MockAuthServiceServer{ctrl: ctrl}
	mock.recorder = &MockAuthServiceServerMockRecorder{mock}
	return mock
}

// EXPECT returns an object that allows the caller to indicate expected use
func (m *MockAuthServiceServer) EXPECT() *MockAuthServiceServerMockRecorder {
	return m.recorder
}

// Login mocks base method
func (m *MockAuthServiceServer) Login(arg0 context.Context, arg1 *proto.LoginRequest) (*proto.LoginReply, error) {
	ret := m.ctrl.Call(m, "Login", arg0, arg1)
	ret0, _ := ret[0].(*proto.LoginReply)
	ret1, _ := ret[1].(error)
	return ret0, ret1
}

// Login indicates an expected call of Login
func (mr *MockAuthServiceServerMockRecorder) Login(arg0, arg1 interface{}) *gomock.Call {
	return mr.mock.ctrl.RecordCallWithMethodType(mr.mock, "Login", reflect.TypeOf((*MockAuthServiceServer)(nil).Login), arg0, arg1)
}

// GetAugmentedToken mocks base method
func (m *MockAuthServiceServer) GetAugmentedToken(arg0 context.Context, arg1 *proto.GetAugmentedAuthTokenRequest) (*proto.GetAugmentedAuthTokenResponse, error) {
	ret := m.ctrl.Call(m, "GetAugmentedToken", arg0, arg1)
	ret0, _ := ret[0].(*proto.GetAugmentedAuthTokenResponse)
	ret1, _ := ret[1].(error)
	return ret0, ret1
}

// GetAugmentedToken indicates an expected call of GetAugmentedToken
func (mr *MockAuthServiceServerMockRecorder) GetAugmentedToken(arg0, arg1 interface{}) *gomock.Call {
	return mr.mock.ctrl.RecordCallWithMethodType(mr.mock, "GetAugmentedToken", reflect.TypeOf((*MockAuthServiceServer)(nil).GetAugmentedToken), arg0, arg1)
}
